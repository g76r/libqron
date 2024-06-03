/* Copyright 2012-2024 Grégoire Barbier and others.
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
#include "alert/alerter.h"
#include "config/eventsubscription.h"
#include "trigger/crontrigger.h"
#include "sysutil/parametrizednetworkrequest.h"
#include "scheduler.h"
#include "condition/disjunctioncondition.h"
#include <QTimer>
#include "pf/pfdomhandler.h"
#include "log/qterrorcodes.h"
#include "thread/blockingtimer.h"
#include "util/regexpparamsprovider.h"

#define PROCESS_OUTPUT_CHUNK_SIZE 16384
#define DEFAULT_STATUS_POLLING_INTERVAL 5000

static QString _localDefaultShell;
static const QRegularExpression _asciiControlCharsSeqRE("[\\0-\\x1f]+");
static const QRegularExpression _whitespace { "\\s+" };

static int staticInit() {
  auto value = qgetenv("SHELL");
  _localDefaultShell = value | "/bin/sh"_u8;
  return 0;
}
Q_CONSTRUCTOR_FUNCTION(staticInit)

static inline Utf8String dockerNameCleanedUp(Utf8String input) {
  return input.isEmpty() || ::isalnum(input.at(0)) ? input : input.sliced(1);
}

Executor::Executor(Scheduler *scheduler) : QObject(0), _isTemporary(false),
  _thread(new QThread),
  _process(0), _nam(new QNetworkAccessManager(this)), _reply(0),
  _alerter(scheduler->alerter()), _abortTimer(new QTimer(this)),
  _statusPollingTimer(new QTimer(this)),
  _scheduler(scheduler), _eventThread(new EventThread) {
  _baseenv = QProcessEnvironment::systemEnvironment();
  _thread->setObjectName(QString("Executor-%1")
                         .arg(reinterpret_cast<long long>(_thread),
                              sizeof(long)*2, 16, QLatin1Char('0')));
  connect(this, &QObject::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  _thread->start();
  moveToThread(_thread);
  _abortTimer->setSingleShot(true);
  connect(_abortTimer, &QTimer::timeout, this, &Executor::abort);
  connect(_statusPollingTimer, &QTimer::timeout, this, &Executor::pollStatus);
  connect(this, &QObject::destroyed, [eventThread=_eventThread]() {
    eventThread->tryPut(EventThread::Event{ });
  });
  _eventThread->start();
}

Executor::~Executor() {
}

void Executor::execute(const TaskInstance &instance) {
  QMetaObject::invokeMethod(this, [this,instance]() {
    _instance = instance;
    _aborting = _retrying = false;
    long long maxDurationBeforeAbort = _instance.task().maxDurationBeforeAbort();
    if (maxDurationBeforeAbort <= INT_MAX)
      _abortTimer->start((int)maxDurationBeforeAbort);
    executeOneTry();
  });
}

void Executor::executeOneTry() {
  _instance.consumeOneTry();
  const auto mean = _instance.task().mean();
  Log::info(_instance.taskId(), _instance.idAsLong())
    << "starting task '" << _instance.taskId() << "' through mean '"
    << Task::meanAsString(mean) << "' after " << _instance.queuedMillis()
    << " ms in queue";
  switch (mean) {
    case Task::Local:
      localMean();
      return;
    case Task::Background:
      backgroundStart();
      return;
    case Task::Ssh:
      sshMean();
      return;
    case Task::Docker:
      dockerMean();
      return;
    case Task::Http:
      httpMean();
      return;
    case Task::Scatter:
      scatterMean();
      return;
    case Task::DoNothing:
      emit taskInstanceStarted(_instance);
      stopOrRetry(true, 0);
      return;
    case Task::UnknownMean: [[unlikely]] // should never happen
      ;
  }
  Log::error(_instance.taskId(), _instance.idAsLong())
    << "cannot execute task with unknown mean '"
    << Task::meanAsString(mean) << "'";
  _instance.consumeAllTries();
  stopOrRetry(false, -1);
}

void Executor::localMean() {
  const auto shell = _instance.paramUtf16("command.shell", _localDefaultShell);
  const auto task = _instance.task();
  QStringList cmdline;
  cmdline << shell << "-c" << PercentEvaluator::eval_utf16(
               task.command(), &_instance);
  Log::info(_instance.taskId(), _instance.idAsLong())
      << "exact command line to be executed (using shell " << shell << "): "
      << cmdline.value(2);
  _instance.setAbortable();
  execProcess(cmdline, true);
}

void Executor::backgroundStart() {
  const auto shell = _instance.paramUtf16("command.shell", _localDefaultShell);
  const auto task = _instance.task();
  if (task.statuscommand().isEmpty()) {
    _instance.consumeAllTries();
    Log::error(_instance.taskId(), _instance.idAsLong())
      << "cannot execute background task without statuscommand '"
      << _instance.taskId() << "'";
    stopOrRetry(false, -1);
    return;
  }
  QStringList cmdline;
  cmdline << shell << "-c" << PercentEvaluator::eval_utf16(
               task.command(), &_instance);
  Log::info(_instance.taskId(), _instance.idAsLong())
    << "exact command line to be executed (using shell " << shell << "): "
    << cmdline.value(2);
  if (!task.abortcommand().isEmpty())
    _instance.setAbortable();
  _backgroundStatus = Starting;
  const int pollingInterval =
      _instance.paramNumber<int>("command.status.interval",
                                 DEFAULT_STATUS_POLLING_INTERVAL);
  _statusPollingTimer->start(pollingInterval);
  execProcess(cmdline, true);
  if (_process)
    QTimer::singleShot(10000, _process, &QProcess::terminate);
}

void Executor::pollStatus() {
  switch (_backgroundStatus) {
    case Starting:
    case Aborting: // abort command is already running, cannot poll status
      return;
    case Started:
      ;
  }
  const auto shell = _instance.paramUtf16("command.shell", _localDefaultShell);
  const auto task = _instance.task();
  QStringList cmdline;
  cmdline << shell << "-c" << PercentEvaluator::eval_utf16(
               task.statuscommand(), &_instance);
  execProcess(cmdline, true);
  if (_process)
    QTimer::singleShot(10000, _process, &QProcess::terminate);
}

void Executor::backgroundAbort() {
  switch(_backgroundStatus) {
    case Starting:
      Log::info(_instance.taskId(), _instance.idAsLong())
        << "cannot abort task because it's not yet started";
      return;
    case Aborting:
      Log::info(_instance.taskId(), _instance.idAsLong())
        << "cannot abort task because it's already being aborted";
      return;
    case Started:
      break;
  }
  const auto shell = _instance.paramUtf16("command.shell", _localDefaultShell);
  const auto task = _instance.task();
  QStringList cmdline;
  cmdline << shell << "-c" << PercentEvaluator::eval_utf16(
               task.abortcommand(), &_instance);
  _backgroundStatus = Aborting;
  execProcess(cmdline, true);
  if (_process)
    QTimer::singleShot(10000, _process, &QProcess::terminate);
}

void Executor::sshMean() {
  QStringList cmdline, sshCmdline;
  const auto params = _instance.params();
  const auto username = params.paramUtf16("ssh.username");
  const auto port = params.paramNumber<int>("ssh.port", -1);
  const auto ignoreknownhosts = params.paramBool("ssh.ignoreknownhosts", true);
  const auto identity = params.paramUtf16("ssh.identity");
  const auto options = params.paramUtf16("ssh.options")
                       .split(_whitespace, Qt::SkipEmptyParts);
  const auto disablepty = params.paramBool("ssh.disablepty", false);
  const auto shell = params.paramUtf16("command.shell");
  const auto task = _instance.task();
  sshCmdline << "ssh" << "-oLogLevel=ERROR" << "-oEscapeChar=none"
             << "-oServerAliveInterval=10" << "-oServerAliveCountMax=3"
             << "-oIdentitiesOnly=yes" << "-oKbdInteractiveAuthentication=no"
             << "-oBatchMode=yes" << "-oConnectionAttempts=3"
             << "-oTCPKeepAlive=yes" << "-oPasswordAuthentication=false";
  if (!disablepty) {
    sshCmdline << "-t" << "-t";
    _instance.setAbortable();
  }
  if (ignoreknownhosts)
    sshCmdline << "-oUserKnownHostsFile=/dev/null"
               << "-oGlobalKnownHostsFile=/dev/null"
               << "-oStrictHostKeyChecking=no";
  if (port > 0 && port < 65536)
    sshCmdline << "-oPort="+QString::number(port);
  if (!identity.isEmpty())
    sshCmdline << "-oIdentityFile=" + identity;
  for (auto option: options)
    sshCmdline << "-o" + option;
  if (!username.isEmpty())
    sshCmdline << "-oUser=" + username;
  sshCmdline << "--";
  sshCmdline << _instance.target().hostname();
  const auto vars = _instance.varsAsEnv();
  for (auto key: vars.keys())
    cmdline << key+"='"+vars.value(key).remove('\'')+"'";
  if (!shell.isEmpty()) {
    cmdline << shell << "-c";
    // must quote command line because remote user default shell will parse and
    // interpretate it and we want to keep it as is in -c argument to choosen
    // shell
    cmdline << '\'' + PercentEvaluator::eval_utf16(
                 task.command(), &_instance).replace("'", "'\\''")
               + '\'';
  } else {
    // let remote user default shell interpretate command line
    cmdline << PercentEvaluator::eval_utf16(task.command(), &_instance);
  }
  Log::info(_instance.taskId(), _instance.idAsLong())
      << "exact command line to be executed (through ssh on host "
      << _instance.target().hostname() <<  "): " << cmdline;
  sshCmdline << cmdline;
  execProcess(sshCmdline, false);
}

void Executor::dockerParam(
    QString *cmdline, const Utf8String &key, const ParamsProvider *context,
    const ParamSet &params, const QString &def) const {
  auto value = params.paramUtf16("docker."+key, def, context);
  if (!value.isEmpty())
    *cmdline += "--"+key+" '"+value.remove('\'')+"' ";
}

void Executor::dockerArrayParam(
    QString *cmdline, const Utf8String &key, const ParamsProvider *context,
    const ParamSet &params, const QString &def) const {
  auto values = params.paramUtf16("docker."+key, def, context)
                .split(_whitespace, Qt::SkipEmptyParts);
  for (auto value: values)
    *cmdline += "--"+key+" '"+value.remove('\'')+"' ";
}

void Executor::dockerMean() {
  const auto params = _instance.params();
  const auto shell = params.paramUtf16("command.shell", _localDefaultShell);
  const auto task = _instance.task();
  QString cmdline;
  const auto image = params.paramUtf16("docker.image", &_instance).remove('\'');
  const bool shouldPull = params.paramBool("docker.pull", true, &_instance);
  const bool shouldInit = params.paramBool("docker.init", true, &_instance);
  const bool shouldRm = params.paramBool("docker.rm", true, &_instance);
  if (image.isEmpty()) {
    _instance.consumeAllTries();
    Log::error(_instance.taskId(), _instance.idAsLong())
        << "cannot execute container with empty image name '"
        << _instance.taskId() << "'";
    stopOrRetry(false, -1);
    return;
  }
  if (shouldPull)
    cmdline += "docker pull '" + image + "' && ";
  cmdline += "exec docker run ";
  if (shouldInit)
    cmdline += "--init -e TINI_KILL_PROCESS_GROUP=1 ";
  if (shouldRm)
    cmdline += "--rm ";
  const auto vars = _instance.varsAsEnv();
  for (auto key: vars.keys())
    cmdline += "-e " + key + "='" + vars.value(key).remove('\'') + "' ";
  dockerParam(&cmdline, "name", &_instance, params,
              dockerNameCleanedUp(_instance.taskId())+"_"+_instance.id()+"_"
              +Utf8String::number(_instance.currentTry()));
  dockerArrayParam(&cmdline, "mount", &_instance, params);
  dockerArrayParam(&cmdline, "tmpfs", &_instance, params);
  dockerArrayParam(&cmdline, "volume", &_instance, params);
  dockerArrayParam(&cmdline, "volumes-from", &_instance, params);
  dockerArrayParam(&cmdline, "publish", &_instance, params);
  dockerArrayParam(&cmdline, "expose", &_instance, params);
  dockerArrayParam(&cmdline, "label", &_instance, params);
  dockerParam(&cmdline, "ipc", &_instance, params);
  dockerParam(&cmdline, "network", &_instance, params);
  dockerParam(&cmdline, "pid", &_instance, params);
  dockerParam(&cmdline, "memory", &_instance, params, "512m");
  dockerParam(&cmdline, "cpus", &_instance, params, "1.0");
  cmdline += "--ulimit core=0 --ulimit nproc=1024 --ulimit nofile=1024 ";
  dockerArrayParam(&cmdline, "ulimit", &_instance, params);
  dockerArrayParam(&cmdline, "device-read-bps", &_instance, params);
  dockerArrayParam(&cmdline, "device-read-iops", &_instance, params);
  dockerArrayParam(&cmdline, "device-write-bps", &_instance, params);
  dockerArrayParam(&cmdline, "device-write-iops", &_instance, params);
  dockerParam(&cmdline, "hostname", &_instance, params);
  dockerArrayParam(&cmdline, "add-host", &_instance, params);
  dockerArrayParam(&cmdline, "dns", &_instance, params);
  dockerArrayParam(&cmdline, "dns-search", &_instance, params);
  dockerArrayParam(&cmdline, "dns-option", &_instance, params);
  dockerParam(&cmdline, "user", &_instance, params);
  dockerArrayParam(&cmdline, "group-add", &_instance, params);
  dockerParam(&cmdline, "workdir", &_instance, params);
  dockerParam(&cmdline, "entrypoint", &_instance, params);
  cmdline += "'" + image + "' "
      + PercentEvaluator::eval_utf16(task.command(), &_instance);
  _instance.setAbortable();
  execProcess({ shell, "-c", cmdline }, false);
}

void Executor::execProcess(QStringList cmdline, bool useVarsAsEnv) {
  QProcessEnvironment sysenv;
  if (useVarsAsEnv) {
    auto env = _instance.varsAsEnv();
    for (auto key: env.keys())
      sysenv.insert(key, env.value(key));
  } else {
    sysenv = _baseenv;
  }
  if (cmdline.isEmpty()) {
    _instance.consumeAllTries();
    Log::warning(_instance.taskId(), _instance.idAsLong())
        << "cannot execute task with empty command '"
        << _instance.taskId() << "'";
    stopOrRetry(false, -1);
    return;
  }
  _errBuf.clear();
  _outBuf.clear();
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
  Log::debug(_instance.taskId(), _instance.idAsLong())
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
  if (!_process)
    return; // LATER add log
  readyReadStandardError();
  readyReadStandardOutput();
  Log::warning(_instance.taskId(), _instance.idAsLong()) // TODO info if aborting
      << "task error #" << error << " : " << _process->errorString();
  _process->kill();
  processFinished(-1, QProcess::CrashExit);
}

void Executor::processFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  if (!_process)
    return; // LATER add log
  const auto task = _instance.task();
  readyReadStandardError();
  readyReadStandardOutput();
  bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
  bool stopping = true;
  switch (task.mean()) {
    case Task::Local:
    case Task::Ssh:
    case Task::Docker:
      success = _instance.paramBool("return.code.default.success", success);
      success = _instance.paramBool(
                  "return.code."+Utf8String::number(exitCode)+".success",
                  success);
      break;
    case Task::Background:
      stopping = false;
      switch(_backgroundStatus) {
        case Starting:
          _backgroundStatus = Started;
          if (exitCode != 0) {
            Log::warning(_instance.taskId(), _instance.idAsLong())
              << "background task '" << _instance.taskId()
              << "' failed, starting command finished with return code "
              << exitCode << " after "<< _instance.durationMillis()
              << " ms of main task duration : " << _process->errorString();
            stopping = true;
            success = false;
            break;
          }
          Log::info(_instance.taskId(), _instance.idAsLong())
            << "background task '" << _instance.taskId() << "' started, "
            << "starting command finished with return code " << exitCode
            << " after "<< _instance.durationMillis()
            << " ms of main task duration";
          break;
        case Started:
          Log::debug(_instance.taskId(), _instance.idAsLong())
            << "background task '" << _instance.taskId() << "' running, "
            << "status command finished with return code " << exitCode
            << " after "<< _instance.durationMillis()
            << " ms of main task duration";
          if (exitCode == 0) // 0: still running
            break;
          stopping = true;
          success = (exitCode == 1); // 1: succeeded 2+: failed
          exitCode = _instance.paramNumber<int>("return.code", -1);
          break;
        case Aborting:
          _backgroundStatus = Started;
          Log::info(_instance.taskId(), _instance.idAsLong())
            << "background task '" << _instance.taskId() << "' aborting, "
            << "aborting command finished with return code " << exitCode
            << " after "<< _instance.durationMillis()
            << " ms of main task duration";
          QMetaObject::invokeMethod(this, &Executor::pollStatus,
                                    Qt::QueuedConnection);
          break;
      }
      break;
    case Task::Http: [[unlikely]]
    case Task::UnknownMean: [[unlikely]]
    case Task::DoNothing: [[unlikely]]
    case Task::Scatter: [[unlikely]]
      ; // should never happen
  }
  /* Qt doc is not explicit if delete should only be done when
     * QProcess::finished() is emited, but we get here too when
     * QProcess::error() is emited.
     * In the other hand it is not sure that finished() is always emited
     * after an error(), may be in some case error() can be emited alone. */
  _process->deleteLater();
  _process = 0;
  _errBuf.clear();
  _outBuf.clear();
  if (stopping)
    stopOrRetry(success, exitCode);
}

void Executor::processProcessOutput(bool isStderr) {
  QByteArray ba, &buf = isStderr ? _errBuf : _outBuf;
  const auto task = _instance.task();
  bool parsecommands = task.mean() == Task::Background;
  if (!_outputSubsInitialized) {
    _stdoutSubs = _scheduler->config().tasksRoot().onstdout()
                  + task.taskGroup().onstdout()
                  + task.onstdout();
    _stderrSubs = _scheduler->config().tasksRoot().onstderr()
                  + task.taskGroup().onstderr()
                  + task.onstderr();
    _outputSubsInitialized = true;
  }
  QList<EventSubscription> subs = isStderr ? _stderrSubs : _stdoutSubs;
  if (subs.isEmpty() && !parsecommands) {
    while (!_process->read(PROCESS_OUTPUT_CHUNK_SIZE).isEmpty());
    return;
  }
  auto ppm = ParamsProviderMerger(&_instance);
  while (!(ba = _process->read(PROCESS_OUTPUT_CHUNK_SIZE)).isEmpty()) {
    buf.append(ba);
    int i;
    while (((i = buf.indexOf('\n')) >= 0)) {
      QString line;
      if (i > 0 && buf.at(i-1) == '\r')
        line = QString::fromUtf8(buf.mid(0, i-1)).trimmed();
      else
        line = QString::fromUtf8(buf.mid(0, i)).trimmed();
      buf.remove(0, i+1);
      line.remove(_asciiControlCharsSeqRE);
      if (line.isEmpty())
        continue;
      ppm.overrideParamValue("line", line);
      if (parsecommands && line.startsWith("!qron:")) {
        PfDomHandler pdh;
        PfParser pp(&pdh);
        pp.parse(line.sliced(6).toUtf8());
        if (pdh.errorOccured()) {
          QString errorString = pdh.errorString()+" at line "
                                +QString::number(pdh.errorLine())
                                +" column "+QString::number(pdh.errorColumn());
          Log::error(_instance.taskId(), _instance.idAsLong())
            << "cannot parse !qron: command in task output: " << errorString;
          continue;
        }
        PfNode root("!qron:");
        root.appendChildren(pdh.roots());
        EventSubscription sub("!qron:", root, _scheduler, {});
        (void)sub.triggerActions(&ppm, _instance);
        continue;
      }
      QList<EventSubscription> filteredSubs;
      for (auto sub: subs) {
        if (sub.filter().match(line).hasMatch())
          filteredSubs.append(sub);
      }
      if (filteredSubs.isEmpty())
        continue;
      _eventThread->tryPut(
          EventThread::Event{ filteredSubs, &ppm, _instance, line });
    }
  }
}

void Executor::readyReadStandardError() {
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardError);
  processProcessOutput(true);
}

void Executor::readyReadStandardOutput() {
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardOutput);
  bool isStderr = false;
  const auto task = _instance.task();
  // process stdout as if it were stderr if
  // - mergestdoutinstderr is set
  // - or for ssh mean with pty allocation enabled (the default)
  if (task.mergeStdoutIntoStderr()
      || (task.mean() == Task::Ssh && !_instance.paramBool("ssh.disablepty"))) {
    Log::debug(_instance.taskId(), _instance.idAsLong())
        << "pretends stdout is stderr";
    isStderr = true;
  }
  processProcessOutput(isStderr);
}

void Executor::httpMean() {
  const auto task = _instance.task();
  QString command = task.command();
  if (command.size() && command.at(0) == '/')
    command = command.mid(1);
  QString url = "http://"+_instance.target().hostname()+"/"+command;
  const auto params = _instance.params();
  ParametrizedNetworkRequest networkRequest(
        url, params, &_instance, _instance.taskId(), _instance.idAsLong());
  const auto vars = _instance.varsAsHeaders();
  for (auto key: vars.keys())
    networkRequest.setRawHeader(key.toLatin1(), vars.value(key).toUtf8());
  // LATER read request output, at less to avoid server being blocked and request never finish
  if (networkRequest.url().isValid()) {
    _instance.setAbortable();
    emit taskInstanceStarted(_instance);
    _reply = networkRequest.performRequest(_nam, QString(), &_instance);
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
      Log::error(_instance.taskId(), _instance.idAsLong())
          << "cannot start HTTP request";
      stopOrRetry(false, -1);
    }
  } else {
    _instance.consumeAllTries();
    Log::error(_instance.taskId(), _instance.idAsLong())
        << "unsupported HTTP URL: "
        << networkRequest.url().toString(QUrl::RemovePassword);
    stopOrRetry(false, -1);
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
  const auto task = _instance.task();
  int maxsize = task.paramNumber<int>(maxsizeKey, 4096);
  int maxwait = (int)(task.paramNumber<double>(maxwaitKey, 5.0)*1000);
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
  QString taskId = _instance.taskId();
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
    success = _instance.paramBool("return.code.default.success", success);
    success = _instance.paramBool("return.code."+Utf8String::number(status)
                                  +".success", success);
    if (success) {
      QString replyValidationPattern =
          _instance.paramUtf16("reply.validation.pattern");
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
  if (!success || _instance.paramBool("log.reply.onsuccess", false)) {
    getReplyContent(reply, &replyContent, QStringLiteral("log.reply.maxsize"),
                    QStringLiteral("log.reply.maxwait"));
    Log::info(taskId, _instance.idAsLong())
        << "HTTP reply began with: "
        << replyContent; // FIXME .replace(asciiControlCharsRE, QStringLiteral(" "));
  }
  Log::log(success ? Log::Info : Log::Warning, taskId, _instance.idAsLong())
      << "task '" << taskId << "' stopped "
      << (success ? "successfully" : "in failure") << " with return code "
      << status << " (" << reason << ") on host '"
      << _instance.target().hostname() << "' in "
      << _instance.runningMillis()
      << " ms, with network error '" << networkErrorAsString(error)
      << "' (QNetworkReply::NetworkError code " << error << ")";
  reply->deleteLater();
  _reply = 0;
  stopOrRetry(success, status);
}

void Executor::scatterMean() {
  const auto task = _instance.task();
  const QString command = task.command();
  const auto params = _instance.params();
  const auto vars = task.vars();
  const auto inputs = params.paramUtf8("scatter.input", &_instance)
                      .split(Utf8String::AsciiWhitespace, Qt::SkipEmptyParts);
  const QRegularExpression regexp(params.paramUtf16("scatter.regexp", ".*"));
  const auto tiiparam = params.paramRawUtf8(
        "scatter.tiiparam", "scatter_children_tii_"+_instance.id())
      .trimmed();
  const auto force = params.paramBool("scatter.force", false, &_instance);
  const auto lone = params.paramBool("scatter.lone", false, &_instance);
  const auto onlast = params.paramUtf8("scatter.onlast", &_instance);
  const auto onlast_condition = params.paramUtf8(
        "scatter.onlast.condition", "allfinished", &_instance);
  // LATER const auto mean = params.value("scatter.mean", "plantask", &_instance);
  // LATER queuewhen ?
  TaskInstanceList instances;
  emit taskInstanceStarted(_instance);
  int rank = -1;
  for (auto input: inputs) {
    ++rank;
    const auto match = regexp.match(input);
    const auto rpp = RegexpParamsProvider(match);
    auto ppm = ParamsProviderMerger(&_instance);
    if (match.hasMatch())
      ppm.prepend(&rpp);
    else
      ppm.overrideParamValue("0", input); // %0 will be available anyway
    auto taskid = PercentEvaluator::eval_utf8(command, &ppm);
    const auto idIfLocalToGroup = task.taskGroup().id()+"."+taskid;
    if (_scheduler->taskExists(idIfLocalToGroup))
      taskid = idIfLocalToGroup;
    ParamSet overridingParams;
    for (auto key: vars.paramKeys())
      overridingParams.insert(
            key, PercentEvaluator::escape(
              PercentEvaluator::eval_utf8(vars.paramRawUtf8(key), &ppm)));
    auto instance =
        _scheduler->planTask(
          taskid, overridingParams, force, lone ? 0 : _instance.herdid(), {},
          {}, _instance.idAsLong(), "scatter"_u8);
    if (instance.isNull()) {
      Log::error(_instance.taskId(), _instance.idAsLong())
          << "scatter failed to plan task : " << taskid << overridingParams
          << force;
      continue;
    }
    ppm.prepend(&instance);
    _scheduler->taskInstanceParamAppend(
          lone ? instance.herdid() : _instance.herdid(), tiiparam,
          instance.id());
    instances << instance;
    if (rank == inputs.size()-1 && !onlast.isEmpty()) {
      auto taskid = PercentEvaluator::eval_utf8(onlast, &ppm);
      const auto idIfLocalToGroup = task.taskGroup().id()+"."
          +taskid;
      if (_scheduler->taskExists(idIfLocalToGroup))
        taskid = idIfLocalToGroup;
      ParamSet overridingParams;
      for (auto key: vars.paramKeys()) {
        auto value = PercentEvaluator::eval_utf8(vars.paramRawUtf8(key), &ppm);
        overridingParams.insert(key, PercentEvaluator::escape(value));
      }
      auto onlastinstance =
          _scheduler->planTask(
            taskid, overridingParams, force,
            lone ? instance.herdid() : _instance.herdid(),
            DisjunctionCondition({PfNode(onlast_condition, "%"+tiiparam)}),
            {}, _instance.idAsLong(), "onlastinstance"_u8);
      if (onlastinstance.isNull()) {
        Log::error(_instance.taskId(), _instance.idAsLong())
            << "scatter failed to plan onlast task : " << taskid
            << overridingParams << force;
        continue;
      }
      instances << onlastinstance;
    }
  }
  Log::debug(_instance.taskId(), _instance.idAsLong())
      << "scatter planned " << instances.size() << " tasks : "
      << instances.join(' ');
  //Log::error(_instance.taskId(), _instance.idAsLong()) << "cannot start HTTP request";
  //taskInstanceStopping(false, -1);
  stopOrRetry(true, 0);
}

void Executor::noticePosted(QString notice, ParamSet params) {
  params.insert(QStringLiteral("!notice"), notice);
}

void Executor::abort() {
  QMetaObject::invokeMethod(this, [this]() {
    // TODO should return a boolean to indicate if abort was actually done or not
    if (_instance.isNull()) {
      Log::error() << "cannot abort task because this executor is not "
                      "currently responsible for any task";
      return;
    }
    const auto task = _instance.task();
    const auto mean = task.mean();
    if (!_instance.abortable()) {
      if (mean == Task::Ssh)
        Log::warning(_instance.taskId(), _instance.idAsLong())
            << "cannot abort task because ssh tasks are not abortable when "
               "ssh.disablepty is set to true";
      else
        Log::warning(_instance.taskId(), _instance.idAsLong())
            << "cannot abort task because it's not abortable";
      return;
    }
    _aborting = true;
    bool hardkill = false;
    switch(mean) {
      case Task::Local:
      case Task::Ssh:
        hardkill = true;
        [[fallthrough]];
      case Task::Docker:
        if (_process) {
          hardkill = _instance.paramBool("command.hardkill", hardkill);
          Log::info(_instance.taskId(), _instance.idAsLong())
            << "process task abort requested, using "
            << (hardkill ? "hard" : "soft") << " kill method";
          if (hardkill)
            _process->kill();
          else
            _process->terminate();
          return;
        }
        break;
      case Task::Background:
        backgroundAbort();
        return;
      case Task::Http:
        if (_reply) {
          Log::info(_instance.taskId(), _instance.idAsLong())
            << "http task abort requested";
          _reply->abort();
          return;
        }
        break;
      case Task::DoNothing: [[unlikely]]
      case Task::Scatter: [[unlikely]]
      case Task::UnknownMean:  [[unlikely]]
        ; // should never happen
    }
    Log::warning(_instance.taskId(), _instance.idAsLong())
      << "cannot abort task";
  });
}

void Executor::stopOrRetry(bool success, int returnCode) {
  if (_retrying)
    return; // avoid being called when calling QCoreApplication::processEvents()
  const auto task = _instance.task();
  _outputSubsInitialized = false;
  _stdoutSubs.clear();
  _stderrSubs.clear();
  if (!success && _instance.remainingTries() && !_aborting) {
    if (_process) {
      _process->deleteLater();
      _process = 0;
    }
    if (_reply) {
      _reply->deleteLater();
      _reply = 0;
    }
    quint32 ms = task.millisBetweenTries();
    if (ms > 0) {
      Log::info(_instance.taskId(), _instance.idAsLong())
        << "waiting " << ms/1000.0 << " seconds before retrying";
      BlockingTimer t { ms, [this]() -> bool { return _aborting; } };
      t.wait();
    }
    if (!_aborting) {
      _alerter->emitAlert("task.retry."+_instance.taskId());
      Log::warning(_instance.taskId(), _instance.idAsLong())
        << "retrying task '" << _instance.taskId() << "' after "
        << _instance.runningMillis() << " ms already running, still "
        << _instance.remainingTries() << " tries remaining.";
      executeOneTry();
      return;
    }
  }
  _abortTimer->stop();
  _statusPollingTimer->stop();
  _instance.setSuccess(success);
  _instance.setReturnCode(returnCode);
  _instance.setStopDatetime();
  Log::log(success ? Log::Info : Log::Warning, _instance.taskId(),
           _instance.idAsLong())
    << "task '" << _instance.taskId() << "' stopped "
    << (success ? "successfully" : "in failure") << " with return code "
    << returnCode << " on host '" << _instance.target().hostname()
    << "' after running " << _instance.runningMillis()
    << " ms (planned time: " << _instance.plannedMillis() << " ms queued time: "
    << _instance.queuedMillis()
    << " ms) with these timestamps: creation: " << _instance.creationDatetime()
    << " queue: " << _instance.queueDatetime() << " start: "
    << _instance.startDatetime() << " stop: " << _instance.stopDatetime();
  emit taskInstanceStopped(_instance, this);
  _instance = TaskInstance();
}
