/* Copyright 2012-2021 Hallowyn and others.
 * This file is part of qron, see <http://qron.eu/>.
 * Qron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Qron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public License
 * along with qron. If not, see <http://www.gnu.org/licenses/>.
 */
#include "executor.h"
#include <QThread>
#include <QtDebug>
#include <QMetaObject>
#include "log/log.h"
#include <QNetworkAccessManager>
#include <QUrl>
#include <QBuffer>
#include <QNetworkReply>
#include "log/qterrorcodes.h"
#include "alert/alerter.h"
#include "config/eventsubscription.h"
#include "trigger/crontrigger.h"
#include "sysutil/parametrizednetworkrequest.h"
#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif
#include "scheduler.h"
#include "util/paramsprovidermerger.h"
#include "util/regexpparamsprovider.h"

static QString _localDefaultShell;
static const QRegularExpression _httpHeaderForbiddenSeqRE("[^a-zA-Z0-9_\\-]+");
static const QRegularExpression _asciiControlCharsSeqRE("[\\0-\\x1f]+");
static const QRegularExpression _unallowedDockerNameFirstChar{"^[^A-Za-z0-9]"};
static const QRegularExpression _whitespace { "\\s+" };

static int staticInit() {
  char *value = getenv("SHELL");
  _localDefaultShell = value && *value ? value : "/bin/sh";
  return 0;
}
Q_CONSTRUCTOR_FUNCTION(staticInit)

Executor::Executor(Scheduler *scheduler) : QObject(0), _isTemporary(false),
  _stderrWasUsed(false), _thread(new QThread),
  _process(0), _nam(new QNetworkAccessManager(this)), _reply(0),
  _alerter(scheduler->alerter()), _abortTimeout(new QTimer(this)),
  _scheduler(scheduler) {
  _baseenv = QProcessEnvironment::systemEnvironment();
  _thread->setObjectName(QString("Executor-%1")
                         .arg(reinterpret_cast<long long>(_thread),
                              sizeof(long)*2, 16, QLatin1Char('0')));
  connect(this, &QObject::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  _thread->start();
  moveToThread(_thread);
  _abortTimeout->setSingleShot(true);
  connect(_abortTimeout, &QTimer::timeout, this, &Executor::abort);
  //qDebug() << "creating new task executor" << this;
}

Executor::~Executor() {
  //qDebug() << "~Executor" << this;
}

void Executor::execute(TaskInstance instance) {
  QMetaObject::invokeMethod(this, [this,instance]() {
    _instance = instance;
    const Task::Mean mean = _instance.task().mean();
    Log::info(_instance.task().id(), _instance.idAsLong())
        << "starting task '" << _instance.task().id() << "' through mean '"
        << Task::meanAsString(mean) << "' after " << _instance.queuedMillis()
        << " ms in queue";
    _stderrWasUsed = false;
    long long maxDurationBeforeAbort = _instance.task().maxDurationBeforeAbort();
    if (maxDurationBeforeAbort <= INT_MAX)
      _abortTimeout->start((int)maxDurationBeforeAbort);
    switch (mean) {
    case Task::Local:
      localMean();
      break;
    case Task::Ssh:
      sshMean();
      break;
    case Task::Docker:
      dockerMean();
      break;
    case Task::Http:
      httpMean();
      break;
    case Task::Scatter:
      scatterMean();
      break;
    case Task::DoNothing:
      emit taskInstanceStarted(_instance);
      taskInstanceStopping(true, 0);
      break;
    default:
      Log::error(_instance.task().id(), _instance.idAsLong())
          << "cannot execute task with unknown mean '"
          << Task::meanAsString(mean) << "'";
      taskInstanceStopping(false, -1);
    }
  });
}

void Executor::localMean() {
  const auto ppp = _instance.pseudoParams();
  const auto params = _instance.params();
  QString shell = params
      .value(QStringLiteral("command.shell"), _localDefaultShell);
  QStringList cmdline;
  cmdline << shell << "-c"
          << params.evaluate(_instance.task().command(), &ppp);
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "exact command line to be executed (using shell " << shell << "): "
      << cmdline.value(2);
  _instance.setAbortable();
  execProcess(cmdline, prepareEnv(_instance.task().vars()));
}

void Executor::sshMean() {
  QStringList cmdline, sshCmdline;
  const auto ppp = _instance.pseudoParams();
  const auto vars = _instance.task().vars();
  const auto params = _instance.params();
  QString username = params.value("ssh.username");
  qlonglong port = params.valueAsLong("ssh.port");
  QString ignoreknownhosts = params.value("ssh.ignoreknownhosts", "true");
  QString identity = params.value("ssh.identity");
  QStringList options = params.valueAsStrings("ssh.options");
  bool disablepty = params.valueAsBool("ssh.disablepty", false);
  QString shell = params.value(QStringLiteral("command.shell"));
  sshCmdline << "ssh" << "-oLogLevel=ERROR" << "-oEscapeChar=none"
             << "-oServerAliveInterval=10" << "-oServerAliveCountMax=3"
             << "-oIdentitiesOnly=yes" << "-oKbdInteractiveAuthentication=no"
             << "-oBatchMode=yes" << "-oConnectionAttempts=3"
             << "-oTCPKeepAlive=yes" << "-oPasswordAuthentication=false";
  if (!disablepty) {
    sshCmdline << "-t" << "-t";
    _instance.setAbortable();
  }
  if (ignoreknownhosts == "true")
    sshCmdline << "-oUserKnownHostsFile=/dev/null"
               << "-oGlobalKnownHostsFile=/dev/null"
               << "-oStrictHostKeyChecking=no";
  if (port > 0 && port < 65536)
    sshCmdline << "-oPort="+QString::number(port);
  if (!identity.isEmpty())
    sshCmdline << "-oIdentityFile=" + identity;
  foreach (QString option, options)
    sshCmdline << "-o" + option;
  if (!username.isEmpty())
    sshCmdline << "-oUser=" + username;
  sshCmdline << "--";
  sshCmdline << _instance.target().hostname();
  for (auto key: vars.keys()) {
      QString value = params.evaluate(vars.rawValue(key), &ppp);
      cmdline << key.remove('\'')+"='"+value.remove('\'')+"'";
  }
  if (!shell.isEmpty()) {
    cmdline << shell << "-c";
    // must quote command line because remote user default shell will parse and
    // interpretate it and we want to keep it as is in -c argument to choosen
    // shell
    cmdline << '\'' + params.evaluate(
                 _instance.task().command(), &ppp).replace("'", "'\\''") + '\'';
  } else {
    // let remote user default shell interpretate command line
    cmdline << params.evaluate(_instance.task().command(), &ppp);
  }
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "exact command line to be executed (through ssh on host "
      << _instance.target().hostname() <<  "): " << cmdline;
  sshCmdline << cmdline;
  execProcess(sshCmdline, _baseenv);
}

void Executor::dockerParam(
    QString *cmdline, QString paramName, const ParamsProvider *context,
    ParamSet instanceParams, QString defaultValue) const {
  auto value = instanceParams.value(
        "docker."+paramName, defaultValue, true, context);
  if (!value.isEmpty())
    *cmdline += "--"+paramName+" '"+value.remove('\'')+"' ";
}

void Executor::dockerArrayParam(
    QString *cmdline, QString paramName, const ParamsProvider *context,
    ParamSet instanceParams, QString defaultValue) const {
  auto values = instanceParams.valueAsStrings(
        "docker."+paramName, defaultValue, true, context);
  for (auto value: values)
    *cmdline += "--"+paramName+" '"+value.remove('\'')+"' ";
}

void Executor::dockerMean() {
  const auto params = _instance.params();
  QString shell = params.value(QStringLiteral("command.shell"),
                               _localDefaultShell);
  QString cmdline;
  const auto ppp = _instance.pseudoParams();
  const auto vars = _instance.task().vars();
  const auto image = params.value("docker.image", &ppp).remove('\'');
  const bool shouldPull = params.valueAsBool("docker.pull", true);
  const bool shouldInit = params.valueAsBool("docker.init", true);
  const bool shouldRm = params.valueAsBool("docker.rm", true);
  if (image.isEmpty()) {
    Log::warning(_instance.task().id(), _instance.idAsLong())
        << "cannot execute container with empty image name '"
        << _instance.task().id() << "'";
    taskInstanceStopping(false, -1);
    return;
  }
  if (shouldPull)
    cmdline += "docker pull '" + image + "' && ";
  cmdline += "exec docker run ";
  if (shouldInit)
    cmdline += "--init -e TINI_KILL_PROCESS_GROUP=1 ";
  if (shouldRm)
    cmdline += "--rm ";
  for (auto key: vars.keys())
      cmdline += "-e '" + key.remove('\'') + "'='"
          + params.evaluate(vars.rawValue(key), &ppp).remove('\'') + "' ";
  dockerParam(&cmdline, "name", &ppp, params,
              _instance.task().id().remove(_unallowedDockerNameFirstChar)
              +"_"+_instance.id());
  dockerArrayParam(&cmdline, "mount", &ppp, params);
  dockerArrayParam(&cmdline, "tmpfs", &ppp, params);
  dockerArrayParam(&cmdline, "volume", &ppp, params);
  dockerArrayParam(&cmdline, "volumes-from", &ppp, params);
  dockerArrayParam(&cmdline, "publish", &ppp, params);
  dockerArrayParam(&cmdline, "expose", &ppp, params);
  dockerArrayParam(&cmdline, "label", &ppp, params);
  dockerParam(&cmdline, "ipc", &ppp, params);
  dockerParam(&cmdline, "network", &ppp, params);
  dockerParam(&cmdline, "pid", &ppp, params);
  dockerParam(&cmdline, "memory", &ppp, params, "512m");
  dockerParam(&cmdline, "cpus", &ppp, params, "1.0");
  cmdline += "--ulimit core=0 --ulimit nproc=1024 --ulimit nofile=1024 ";
  dockerArrayParam(&cmdline, "ulimit", &ppp, params);
  dockerArrayParam(&cmdline, "device-read-bps", &ppp, params);
  dockerArrayParam(&cmdline, "device-read-iops", &ppp, params);
  dockerArrayParam(&cmdline, "device-write-bps", &ppp, params);
  dockerArrayParam(&cmdline, "device-write-iops", &ppp, params);
  dockerParam(&cmdline, "hostname", &ppp, params);
  dockerArrayParam(&cmdline, "add-host", &ppp, params);
  dockerArrayParam(&cmdline, "dns", &ppp, params);
  dockerArrayParam(&cmdline, "dns-search", &ppp, params);
  dockerArrayParam(&cmdline, "dns-option", &ppp, params);
  dockerParam(&cmdline, "user", &ppp, params);
  dockerArrayParam(&cmdline, "group-add", &ppp, params);
  dockerParam(&cmdline, "workdir", &ppp, params);
  dockerParam(&cmdline, "entrypoint", &ppp, params);
  cmdline += "'" + image + "' "
      + params.evaluate(_instance.task().command(), &ppp);
  _instance.setAbortable();
  execProcess({ shell, "-c", cmdline }, _baseenv);
}

void Executor::execProcess(QStringList cmdline, QProcessEnvironment sysenv) {
  if (cmdline.isEmpty()) {
    Log::warning(_instance.task().id(), _instance.idAsLong())
        << "cannot execute task with empty command '"
        << _instance.task().id() << "'";
    taskInstanceStopping(false, -1);
    return;
  }
  _errBuf.clear();
  _process = new QProcess(this);
  _process->setProcessChannelMode(QProcess::SeparateChannels);
  connect(_process, &QProcess::errorOccurred,
          this, &Executor::processError);
  connect(_process, &QProcess::finished,
          this, &Executor::processFinished);
  connect(_process, &QProcess::readyReadStandardError,
          this, &Executor::readyReadStandardError);
  connect(_process, &QProcess::readyReadStandardOutput,
          this, &Executor::readyReadStandardOutput);
  _process->setProcessEnvironment(sysenv);
  QString program = cmdline.takeFirst();
  Log::debug(_instance.task().id(), _instance.idAsLong())
      << "about to start system process '" << program << "' with args "
      << cmdline << " and environment " << sysenv.toStringList();
  emit taskInstanceStarted(_instance);
  _process->start(program, cmdline);
  // detach from process group to avoid the child to receive e.g. SIGINT
  // LATER when upgrading to Qt 6 use QProcess::setChildProcessModifier to
  // setpgid in the child in addition to the parent, there is a race condition
  // but the child is likely to be the right place statistically by far
#ifdef Q_OS_UNIX
  //_process->waitForStarted();
  //int pid = _process->pid();
  //::setpgid(pid, 0);
  //qDebug() << "setpgid" << pid << r;
#endif
}

void Executor::processError(QProcess::ProcessError error) {
  //qDebug() << "************ processError" << _instance.id() << _process;
  if (!_process)
    return; // LATER add log
  readyReadStandardError();
  readyReadStandardOutput();
  Log::warning(_instance.task().id(), _instance.idAsLong()) // TODO info if aborting
      << "task error #" << error << " : " << _process->errorString();
  _process->kill();
  processFinished(-1, QProcess::CrashExit);
}

void Executor::processFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  //qDebug() << "************ processFinished" << _instance.id() << _process;
  if (!_process)
    return; // LATER add log
  readyReadStandardError();
  readyReadStandardOutput();
  bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
  success = _instance.task().params()
      .valueAsBool("return.code.default.success", success);
  success = _instance.task().params()
      .valueAsBool("return.code."+QString::number(exitCode)+".success",success);
  _instance.setStopDatetime();
  Log::log(success ? Log::Info : Log::Warning, _instance.task().id(),
           _instance.idAsLong())
      << "task '" << _instance.task().id() << "' stopped "
      << (success ? "successfully" : "in failure") << " with return code "
      << exitCode << " on host '" << _instance.target().hostname()
      << "' after running " << _instance.runningMillis()
      << " ms (duration including queue: " << _instance.durationMillis()
      << " ms)";
  if (!_stderrWasUsed  && _alerter)
    _alerter->cancelAlert("task.stderr."+_instance.task().id());
  /* Qt doc is not explicit if delete should only be done when
     * QProcess::finished() is emited, but we get here too when
     * QProcess::error() is emited.
     * In the other hand it is not sure that finished() is always emited
     * after an error(), may be in some case error() can be emited alone. */
  _process->deleteLater();
  _process = 0;
  _errBuf.clear();
  taskInstanceStopping(success, exitCode);
}

static QRegularExpression _sshConnClosedRE("^Connection to [^ ]* closed\\.$");

void Executor::readyProcessWarningOutput() {
  // LATER provide a way to define several stderr filter regexps
  // LATER provide a way to choose log level for stderr
  QByteArray ba;
  while (!(ba = _process->read(1024)).isEmpty()) {
    _errBuf.append(ba);
    int i;
    while (((i = _errBuf.indexOf('\n')) >= 0)) {
      QString line;
      if (i > 0 && _errBuf.at(i-1) == '\r')
        line = QString::fromUtf8(_errBuf.mid(0, i-1)).trimmed();
      else
        line = QString::fromUtf8(_errBuf.mid(0, i)).trimmed();
      _errBuf.remove(0, i+1);
      line.remove(_asciiControlCharsSeqRE);
      if (!line.isEmpty()) {
        QList<QRegularExpression> filters(_instance.task().stderrFilters());
        if (filters.isEmpty() && _instance.task().mean() == Task::Ssh)
          filters.append(_sshConnClosedRE);
        for (auto filter: filters)
          if (filter.match(line).hasMatch())
            goto line_filtered;
        Log::warning(_instance.task().id(), _instance.idAsLong())
            << "task stderr: " << line;
        if (!_stderrWasUsed) {
          _stderrWasUsed = true;
          if (_alerter && !_instance.task().params()
              .valueAsBool("disable.alert.stderr", false))
            _alerter->raiseAlert("task.stderr."+_instance.task().id());
        }
line_filtered:;
      }
    }
  }
}

void Executor::readyReadStandardError() {
  //qDebug() << "************ readyReadStandardError" << _instance.task().id() << _instance.id() << _process;
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardError);
  readyProcessWarningOutput();
}

void Executor::readyReadStandardOutput() {
  //qDebug() << "************ readyReadStandardOutput" << _instance.task().id() << _instance.id() << _process;
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardOutput);
  if (_instance.task().mean() == Task::Ssh
      && _instance.params().value("ssh.disablepty") != "true")
    readyProcessWarningOutput(); // with pty, stderr and stdout are merged
  else
    while (!_process->read(1024).isEmpty());
  // LATER make it possible to log stdout too (as debug, depending on task cfg)
}

void Executor::httpMean() {
  QString command = _instance.task().command();
  if (command.size() && command.at(0) == '/')
    command = command.mid(1);
  QString url = "http://"+_instance.target().hostname()+"/"+command;
  const auto ppp = _instance.pseudoParams();
  const auto params = _instance.params();
  ParametrizedNetworkRequest networkRequest(
        url, params, &ppp, _instance.task().id(), _instance.idAsLong());
  foreach (QString name, _instance.task().vars().keys()) {
    const QString rawValue = _instance.task().vars().rawValue(name);
    if (name.endsWith(":")) // ignoring : at end of header name
      name.chop(1);
    name.replace(_httpHeaderForbiddenSeqRE, "_");
    const QString value = params.evaluate(rawValue, &ppp);
    //Log::fatal(_instance.task().id(), _instance.id()) << "setheader: " << name << "=" << value << ".";
    networkRequest.setRawHeader(name.toLatin1(), value.toUtf8());
  }
  // LATER read request output, at less to avoid server being blocked and request never finish
  if (networkRequest.url().isValid()) {
    _instance.setAbortable();
    emit taskInstanceStarted(_instance);
    _reply = networkRequest.performRequest(_nam, QString(), &ppp);
    if (_reply) {
      // note that the apparent critical window between QNAM::get/put/post()
      // and connection to reply signals is not actually critical since
      // QNetworkReply lies in the same thread than QNAM and Executor, and
      // therefore no QNetworkReply slot can executed meanwhile hence no
      // QNetworkReply::finished() cannot be emitted before connection
      // TODO is connection to error() usefull ? can error() be emited w/o finished() ?
      // FIXME connection from error() seems irrelevant since it's not a signal !
      // FIXME replace error with errorOccurred
      connect(_reply, &QNetworkReply::errorOccurred, this, &Executor::replyError);
      connect(_reply, &QNetworkReply::finished, this, &Executor::replyFinished);
    } else {
      Log::error(_instance.task().id(), _instance.idAsLong())
          << "cannot start HTTP request";
      taskInstanceStopping(false, -1);
    }
  } else {
    Log::error(_instance.task().id(), _instance.idAsLong())
        << "unsupported HTTP URL: "
        << networkRequest.url().toString(QUrl::RemovePassword);
    taskInstanceStopping(false, -1);
  }
}

void Executor::replyError(QNetworkReply::NetworkError error) {
  replyHasFinished(qobject_cast<QNetworkReply*>(sender()), error);
}

void Executor::replyFinished() {
  replyHasFinished(qobject_cast<QNetworkReply*>(sender()),
                  QNetworkReply::NoError);
}

void Executor::getReplyContent(QNetworkReply *reply, QString *replyContent,
                               QString maxsizeKey, QString maxwaitKey) const {
  Q_ASSERT(replyContent);
  if (!replyContent->isNull())
    return;
  int maxsize = _instance.task().params().valueAsInt(maxsizeKey, 4096);
  int maxwait = (int)_instance.task().params().valueAsDouble(maxwaitKey, 5.0)*1000;
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 deadline = now+maxwait;
  while (reply->bytesAvailable() < maxsize && now < deadline) {
    if (!reply->waitForReadyRead((int)(deadline-now)))
      break;
    now = QDateTime::currentMSecsSinceEpoch();
  }
  QByteArray ba = reply->read(maxsize);
  *replyContent = ba.isEmpty() ? QLatin1String("") : QString::fromUtf8(ba);
}

// executed by the first emitted signal among QNetworkReply::error() and
// QNetworkReply::finished(), therefore it can be called twice when an error
// occurs (in fact most of the time where an error occurs, but in cases where
// error() is not followed by finished() and I am not sure there are such cases)
void Executor::replyHasFinished(QNetworkReply *reply,
                               QNetworkReply::NetworkError error) {
  QString taskId = _instance.task().id();
  if (!_reply) {
    Log::debug() << "Executor::replyFinished called as it is not responsible "
                    "of any http request";
    // seems normal on some network error ?
    return;
  }
  if (!reply) {
    Log::error(taskId, _instance.idAsLong())
        << "Executor::replyFinished receive null pointer";
    return;
  }
  if (reply != _reply) {
    Log::error(taskId, _instance.idAsLong())
        << "Executor::replyFinished receive unrelated pointer";
    return;
  }
  int status = reply
      ->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QString reason = reply
      ->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
  bool success = false;
  QString replyContent;
  if (status > 0) {
    success =  status >= 200 && status <= 299;
    success = _instance.task().params()
        .valueAsBool(QStringLiteral("return.code.default.success"), success);
    success = _instance.task().params()
        .valueAsBool("return.code."+QString::number(status)+".success",success);
    if (success) {
      QString replyValidationPattern = _instance.task().params().value(
            QStringLiteral("reply.validation.pattern"));
      if (!replyValidationPattern.isEmpty()) {
        QRegularExpression re(replyValidationPattern); // LATER cache
        if (!re.isValid()) {
          Log::warning(taskId, _instance.idAsLong())
              << "invalid reply validation pattern: " << replyValidationPattern;
          success = false;
        } else {
          getReplyContent(reply, &replyContent,
                          QStringLiteral("reply.validation.maxsize"),
                          QStringLiteral("reply.validation.maxwait"));
          if (re.match(replyContent).hasMatch()) {
            Log::info(taskId, _instance.idAsLong())
                << "reply validation pattern matched reply: "
                << replyValidationPattern;
          } else {
            success = false;
            Log::info(taskId, _instance.idAsLong())
                << "reply validation pattern did not match reply: "
                << replyValidationPattern;
          }
        }
      }
    }
  }
  if (!success || _instance.task().params()
      .valueAsBool(QStringLiteral("log.reply.onsuccess"), false)) {
    getReplyContent(reply, &replyContent, QStringLiteral("log.reply.maxsize"),
                    QStringLiteral("log.reply.maxwait"));
    Log::info(taskId, _instance.idAsLong())
        << "HTTP reply began with: "
        << replyContent; // FIXME .replace(asciiControlCharsRE, QStringLiteral(" "));
  }
  _instance.setStopDatetime();
  Log::log(success ? Log::Info : Log::Warning, taskId, _instance.idAsLong())
      << "task '" << taskId << "' stopped "
      << (success ? "successfully" : "in failure") << " with return code "
      << status << " (" << reason << ") on host '"
      << _instance.target().hostname() << "' in " << _instance.runningMillis()
      << " ms, with network error '" << networkErrorAsString(error)
      << "' (QNetworkReply::NetworkError code " << error << ")";
  reply->deleteLater();
  _reply = 0;
  taskInstanceStopping(success, status);
}

void Executor::scatterMean() {
  const QString command = _instance.task().command();
  const auto ppp = _instance.pseudoParams();
  const auto params = _instance.params();
  const auto vars = _instance.task().vars();
  auto ppm = ParamsProviderMerger(params)(&ppp);
  const auto inputs = params.value("scatter.input", &ppp).split(_whitespace);
  const auto regexp = QRegularExpression(params.value("scatter.regexp", ".*"));
  const auto paramappend = params.rawValue("scatter.paramappend").trimmed();
  const auto force = params.valueAsBool("scatter.force", false, true, &ppp);
  const auto lone = params.valueAsBool("scatter.lone", false, true, &ppp);
  const auto herder = lone ? TaskInstance() : _instance.herder();
  // LATER const auto mean = params.value("scatter.mean", "plantask", &ppm);
  // LATER queuewhen ?
  TaskInstanceList instances;

  emit taskInstanceStarted(_instance);
  for (auto input: inputs) {
    const auto match = regexp.match(input);
    const auto rpp = RegexpParamsProvider(match);
    const auto ppmr = ParamsProviderMergerRestorer(ppm);
    if (match.hasMatch())
      ppm.prepend(&rpp);
    else
      ppm.overrideParamValue("0", input); // %0 will be available anyway
    auto taskid = ParamSet().evaluate(command, &ppm);
    const auto idIfLocalToGroup = _instance.task().taskGroup().id()+"."+taskid;
    if (_scheduler->taskExists(idIfLocalToGroup))
      taskid = idIfLocalToGroup;
    ParamSet overridingParams;
    for (auto key: vars.keys()) {
      auto value = ParamSet().evaluate(vars.rawValue(key), &ppm);
      overridingParams.setValue(key, ParamSet::escape(value));
    }
    auto instance = _scheduler->planTask(
        taskid, overridingParams, force, herder, Condition(), Condition())
        .value(0);
    if (instance.isNull()) {
      Log::error(_instance.task().id(), _instance.idAsLong())
          << "scatter failed to plan task : " << taskid << overridingParams
          << force << instance.herdid();
      continue;
    }
    int i = paramappend.indexOf(' ');
    if (i > 0) {
      const auto key = paramappend.left(i);
      const auto rawvalue = paramappend.mid(i+1);
      const auto ppp = instance.pseudoParams();
      ppm.prepend(instance.params()).prepend(&ppp);
      auto value = ParamSet().evaluate(rawvalue, &ppm);
      herder.paramAppend(key, value);
    }
    instances << instance;
  }
  Log::debug(_instance.task().id(), _instance.idAsLong())
      << "scatter planned " << instances.size() << " tasks : "
      << instances.join(' ');
  //Log::error(_instance.task().id(), _instance.idAsLong()) << "cannot start HTTP request";
  //taskInstanceStopping(false, -1);
  taskInstanceStopping(true, 0);
}


void Executor::noticePosted(QString notice, ParamSet params) {
  params.setValue(QStringLiteral("!notice"), notice);
}

static QRegularExpression notIdentifier("[^a-zA-Z_0-9]+");

QProcessEnvironment Executor::prepareEnv(const ParamSet vars) {
  QProcessEnvironment sysenv;
  foreach (QString key, vars.keys()) {
    if (key.isEmpty())
      continue;
    key.replace(notIdentifier, "_");
    if (key[0] >= '0' && key[0] <= '9' )
      key.insert(0, '_');
    const auto ppp = _instance.pseudoParams();
    const QString value = _instance.params().evaluate(vars.rawValue(key), &ppp);
    sysenv.insert(key, value);
  }
  return sysenv;
}

void Executor::abort() {
  QMetaObject::invokeMethod(this, [this]() {
    // TODO should return a boolean to indicate if abort was actually done or not
    if (_instance.isNull()) {
      Log::error() << "cannot abort task because this executor is not "
                      "currently responsible for any task";
    } else if (!_instance.abortable()) {
      if (_instance.task().mean() == Task::Ssh)
        Log::warning(_instance.task().id(), _instance.idAsLong())
            << "cannot abort task because ssh tasks are not abortable when "
               "ssh.disablepty is set to true";
      else
        Log::warning(_instance.task().id(), _instance.idAsLong())
            << "cannot abort task because it is marked as not abortable";
    } else if (_process) {
      Log::info(_instance.task().id(), _instance.idAsLong())
          << "process task abort requested";
      if (_instance.task().mean() == Task::Docker) {
        // LATER use docker run --cidfile and docker kill
        _process->terminate();
      } else {
        // TODO kill the whole process group
        _process->kill();
      }
    } else if (_reply) {
      Log::info(_instance.task().id(), _instance.idAsLong())
          << "http task abort requested";
      _reply->abort();
    } else {
      Log::warning(_instance.task().id(), _instance.idAsLong())
          << "cannot abort task because its execution mean is not abortable";
    }
  });
}

void Executor::taskInstanceStopping(bool success, int returnCode) {
  _abortTimeout->stop();
  _instance.setSuccess(success);
  _instance.setReturnCode(returnCode);
  _instance.setStopDatetime();
  emit taskInstanceStopped(_instance, this);
  _instance = TaskInstance();
}
