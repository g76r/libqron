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
#include "stepinstance.h"
#include "alert/alerter.h"
#include "config/eventsubscription.h"
#include "trigger/crontrigger.h"
#include "util/timerwitharguments.h"
#include "sysutil/parametrizednetworkrequest.h"
#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif

static QString _localDefaultShell;

static int staticInit() {
  char *value = getenv("SHELL");
  _localDefaultShell = value && *value ? value : "/bin/sh";
  return 0;
}
Q_CONSTRUCTOR_FUNCTION(staticInit)

Executor::Executor(Alerter *alerter) : QObject(0), _isTemporary(false),
  _stderrWasUsed(false), _thread(new QThread),
  _process(0), _nam(new QNetworkAccessManager(this)), _reply(0),
  _alerter(alerter), _abortTimeout(new QTimer(this)) {
  _baseenv = QProcessEnvironment::systemEnvironment();
  _thread->setObjectName(QString("Executor-%1")
                         .arg((long long)_thread, sizeof(long)*2, 16,
                              QLatin1Char('0')));
  connect(this, &QObject::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  _thread->start();
  moveToThread(_thread);
  _abortTimeout->setSingleShot(true);
  connect(_abortTimeout, &QTimer::timeout, this, &Executor::doAbort);
  //qDebug() << "creating new task executor" << this;
}

Executor::~Executor() {
  //qDebug() << "~Executor" << this;
}

void Executor::execute(TaskInstance instance) {
  QMetaObject::invokeMethod(this, "doExecute", Q_ARG(TaskInstance, instance));
}

void Executor::doExecute(TaskInstance instance) {
  _instance = instance;
  const Task::Mean mean = _instance.task().mean();
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "starting task '" << _instance.task().id() << "' through mean '"
      << Task::meanAsString(mean) << "' after " << _instance.queuedMillis()
      << " ms in queue";
  _stderrWasUsed = false;
  long long maxDurationBeforeAbort = _instance.task().maxDurationBeforeAbort();
  if (maxDurationBeforeAbort <= INT_MAX)
    _abortTimeout->start(maxDurationBeforeAbort);
  switch (mean) {
  case Task::Local:
    localMean();
    break;
  case Task::Ssh:
    sshMean();
    break;
  case Task::Http:
    httpMean();
    break;
  case Task::Workflow:
    workflowMean();
    break;
  case Task::DoNothing:
    emit taskInstanceStarted(_instance);
    taskInstanceFinishing(true, 0);
    break;
  default:
    Log::error(_instance.task().id(), _instance.idAsLong())
        << "cannot execute task with unknown mean '"
        << Task::meanAsString(mean) << "'";
    taskInstanceFinishing(false, -1);
  }
}

void Executor::localMean() {
  TaskInstancePseudoParamsProvider ppp = _instance.pseudoParams();
  QString shell = _instance.params()
      .value(QStringLiteral("command.shell"), _localDefaultShell);
  QStringList cmdline;
  cmdline << shell << "-c"
          << _instance.params().evaluate(_instance.task().command(), &ppp);
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "exact command line to be executed (using shell " << shell << "): "
      << cmdline.value(2);
  QProcessEnvironment sysenv;
  prepareEnv(&sysenv);
  _instance.setAbortable();
  execProcess(cmdline, sysenv);
}

void Executor::sshMean() {
  QStringList cmdline, sshCmdline;
  TaskInstancePseudoParamsProvider ppp = _instance.pseudoParams();
  QHash<QString,QString> setenv;
  QProcessEnvironment sysenv;
  prepareEnv(&sysenv, &setenv);
  QString username = _instance.params().value("ssh.username");
  qlonglong port = _instance.params().valueAsLong("ssh.port");
  QString ignoreknownhosts = _instance.params().value("ssh.ignoreknownhosts",
                                                    "true");
  QString identity = _instance.params().value("ssh.identity");
  QStringList options = _instance.params().valueAsStrings("ssh.options");
  bool disablepty = _instance.params().valueAsBool("ssh.disablepty", false);
  QString shell = _instance.params().value(QStringLiteral("command.shell"));
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
  foreach (QString key, setenv.keys())
    if (!_instance.task().unsetenv().contains(key)) {
      QString value = setenv.value(key);
      value.replace('\'', QString());
      cmdline << key+"='"+value+"'";
    }
  if (!shell.isEmpty()) {
    cmdline << shell << "-c";
    // must quote command line because remote user default shell will parse and
    // interpretate it and we want to keep it as is in -c argument to choosen
    // shell
    cmdline << '\'' + _instance.params().evaluate(_instance.task().command(),
                                                  &ppp).replace("'", "'\\''")
               + '\'';
  } else {
    // let remote user default shell interpretate command line
    cmdline << _instance.params().evaluate(_instance.task().command(), &ppp);
  }
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "exact command line to be executed (through ssh on host "
      << _instance.target().hostname() <<  "): " << cmdline;
  sshCmdline << cmdline;
  execProcess(sshCmdline, sysenv);
}

void Executor::execProcess(QStringList cmdline, QProcessEnvironment sysenv) {
  if (cmdline.isEmpty()) {
    Log::warning(_instance.task().id(), _instance.idAsLong())
        << "cannot execute task with empty command '"
        << _instance.task().id() << "'";
    taskInstanceFinishing(false, -1);
    return;
  }
  _errBuf.clear();
  _process = new QProcess(this);
  _process->setProcessChannelMode(QProcess::SeparateChannels);
#if QT_VERSION >= 0x050600
  connect(_process, &QProcess::errorOccurred,
          this, &Executor::processError);
#else
  connect(_process, static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
          this, &Executor::processError);
#endif
  connect(_process, static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
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
  _instance.setEndDatetime();
  Log::log(success ? Log::Info : Log::Warning, _instance.task().id(),
           _instance.idAsLong())
      << "task '" << _instance.task().id() << "' finished "
      << (success ? "successfully" : "in failure") << " with return code "
      << exitCode << " on host '" << _instance.target().hostname()
      << "' after running " << _instance.runningMillis()
      << " ms (total time including queue: " << _instance.totalMillis()
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
  taskInstanceFinishing(success, exitCode);
}

static QRegExp sshConnClosed("^Connection to [^ ]* closed\\.$");

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
      if (!line.isEmpty()) {
        QList<QRegExp> filters(_instance.task().stderrFilters());
        if (filters.isEmpty() && _instance.task().mean() == Task::Ssh)
          filters.append(sshConnClosed);
        foreach (QRegExp filter, filters)
          if (filter.indexIn(line) >= 0)
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
  TaskInstancePseudoParamsProvider ppp = _instance.pseudoParams();
  ParametrizedNetworkRequest networkRequest(
        url, _instance.params(), &ppp, _instance.task().id(), _instance.idAsLong());
  foreach (QString name, _instance.task().setenv().keys()) {
    const QString expr(_instance.task().setenv().rawValue(name));
    if (name.endsWith(":")) // ignoring : at end of header name
      name.chop(1);
    name.replace(QRegExp("[^a-zA-Z_0-9\\-]+"), "_");
    const QString value = _instance.params().evaluate(expr);
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
      connect(_reply, static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
              this, &Executor::replyError);
      connect(_reply, &QNetworkReply::finished, this, &Executor::replyFinished);
    } else {
      Log::error(_instance.task().id(), _instance.idAsLong())
          << "cannot start HTTP request";
      taskInstanceFinishing(false, -1);
    }
  } else {
    Log::error(_instance.task().id(), _instance.idAsLong())
        << "unsupported HTTP URL: "
        << networkRequest.url().toString(QUrl::RemovePassword);
    taskInstanceFinishing(false, -1);
  }
}

void Executor::replyError(QNetworkReply::NetworkError error) {
  replyHasFinished(qobject_cast<QNetworkReply*>(sender()), error);
}

// LATER replace with QRegularExpression, but not without regression/unit tests
static QRegExp asciiControlCharsRE("[\\0-\\x1f]+");

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
  int maxwait = _instance.task().params().valueAsDouble(maxwaitKey, 5.0)*1000;
  long now = QDateTime::currentMSecsSinceEpoch();
  long deadline = now+maxwait;
  while (reply->bytesAvailable() < maxsize && now < deadline) {
    if (!reply->waitForReadyRead(deadline-now))
      break;
    now = QDateTime::currentMSecsSinceEpoch();
  }
  QByteArray ba = reply->read(maxsize);
  *replyContent = ba.isEmpty() ? QStringLiteral("") : QString::fromUtf8(ba);
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
  _instance.setEndDatetime();
  Log::log(success ? Log::Info : Log::Warning, taskId, _instance.idAsLong())
      << "task '" << taskId << "' finished "
      << (success ? "successfully" : "in failure") << " with return code "
      << status << " (" << reason << ") on host '"
      << _instance.target().hostname() << "' in " << _instance.runningMillis()
      << " ms, with network error '" << networkErrorAsString(error)
      << "' (QNetworkReply::NetworkError code " << error << ")";
  reply->deleteLater();
  _reply = 0;
  taskInstanceFinishing(success, status);
}

void Executor::workflowMean() {
  foreach (Step step, _instance.task().steps()) {
    StepInstance si(step, _instance);
    _steps.insert(step.id(), si);
  }
  QHash<QString,CronTrigger> workflowCronTriggers =
      _instance.task().workflowCronTriggersByLocalId();
  foreach (QString triggerId, workflowCronTriggers.keys()) {
    CronTrigger trigger = workflowCronTriggers.value(triggerId);
    trigger.detach();
    QDateTime now(QDateTime::currentDateTime());
    trigger.setLastTriggered(now);
    QDateTime next = trigger.nextTriggering();
    if (next.isValid()) {
      qint64 ms = now.msecsTo(next);
      TimerWithArguments *timer = new TimerWithArguments(this);
      timer->setSingleShot(true);
      timer->connectWithArgs(this, "workflowCronTriggered", triggerId);
      timer->setTimerType(Qt::PreciseTimer); // LATER is it really needed ?
      timer->start(ms < INT_MAX ? ms : INT_MAX);
      _workflowTimers.append(timer);
      //Log::fatal(_instance.task().id(), _instance.id())
      //    << "****** configured workflow timer " << id << " " << trigger.expression()
      //    << " to " << ms << " ms";
    } else {
      // this is likely to occur for timers too far in the future (> ~20 days)
      Log::debug(_instance.task().id(), _instance.idAsLong())
          << "invalid workflow timer " << triggerId << " " << next.toString()
          << " " << trigger.humanReadableExpression();
    }
  }
  _instance.setAbortable();
  //Log::fatal(_instance.task().id(), _instance.id())
  //    << "starting workflow";
  emit taskInstanceStarted(_instance);
  //  Log::debug(_instance.task().id(), _instance.id())
  //      << "********* steps: " << _steps.keys();
  //  Log::debug(_instance.task().id(), _instance.id())
  //      << "********* starting " << _instance.task().id()+":$start "
  //      << _steps[_instance.task().id()+":$start"].step().id();
  _steps[_instance.task().id()+":$start"]
      .predecessorReady(WorkflowTransition(), ParamSet());
  //  Log::debug(_instance.task().id(), _instance.id())
  //      << "********* done ";
}

void Executor::activateWorkflowTransition(WorkflowTransition transition,
                                          ParamSet eventContext) {
  QMetaObject::invokeMethod(this, "doActivateWorkflowTransition",
                            Qt::QueuedConnection,
                            Q_ARG(WorkflowTransition, transition),
                            Q_ARG(ParamSet, eventContext));
}

void Executor::doActivateWorkflowTransition(WorkflowTransition transition,
                                            ParamSet eventContext) {
  // if target is "$end", finish workflow
  if (transition.targetLocalId() == QStringLiteral("$end")) {
    finishWorkflow(eventContext.valueAsBool(QStringLiteral("!success"), true),
                   eventContext.valueAsInt(QStringLiteral("!returncode"), 0));
    return;
  }
  // otherwise this is a regular step to step transition
  QString targetStepId = _instance.task().id()+":"+transition.targetLocalId();
  if (!_steps.contains(targetStepId)) {
    Log::error(_instance.task().id(), _instance.idAsLong())
        << "unknown step id in transition id: " << transition.id();
    return;
  }
  Log::debug(_instance.task().id(), _instance.idAsLong())
      << "activating workflow transition " << transition.id();
  _steps[targetStepId].predecessorReady(transition, eventContext);
}

void Executor::noticePosted(QString notice, ParamSet params) {
  params.setValue(QStringLiteral("!notice"), notice);
  Step step = _instance.task().steps().value("$noticetrigger_"+notice);
  foreach (EventSubscription es, step.onreadyEventSubscriptions()) {
    Log::debug(_instance.task().id(), _instance.idAsLong())
        << "triggering " << step.localId() << " "
        << step.trigger().humanReadableExpressionWithCalendar();
    // MAYDO overriding params with (evaluated) trigger overriding params
    es.triggerActions(params, _instance);
  }
}

void Executor::workflowCronTriggered(QVariant sourceLocalId) {
  Step step = _instance.task().steps().value(sourceLocalId.toString());
  foreach (EventSubscription es, step.onreadyEventSubscriptions()) {
    Log::debug(_instance.task().id(), _instance.idAsLong())
        << "triggering " << step.localId() << " "
        << step.trigger().humanReadableExpressionWithCalendar();
    // MAYDO use (evaluated) trigger overriding params
    es.triggerActions(ParamSet(), _instance);
  }
}

void Executor::finishWorkflow(bool success, int returnCode) {
  // TODO abort running steps and other workflow cleanup work
  Log::info(_instance.task().id(), _instance.idAsLong())
      << "ending workflow in " << (success ? "success" : "failure")
      << " with return code " << returnCode;
  taskInstanceFinishing(success, returnCode);
}

static QRegularExpression notIdentifier("[^a-zA-Z_0-9]+");

void Executor::prepareEnv(QProcessEnvironment *sysenv,
                          QHash<QString,QString> *setenv) {
  if (_instance.task().params().valueAsBool(QStringLiteral("clearsysenv")))
    *sysenv = QProcessEnvironment();
  else
    *sysenv = _baseenv;
  // first clean system base env from any unset variables
  foreach (const QString pattern, _instance.task().unsetenv().keys()) {
    QRegExp re(pattern, Qt::CaseInsensitive, QRegExp::WildcardUnix);
    foreach (const QString key, sysenv->keys())
      if (re.exactMatch(key))
        sysenv->remove(key);
  }
  // then build setenv evaluated paramset that may be used apart from merging
  // into sysenv
  foreach (QString key, _instance.task().setenv().keys()) {
    if (key.isEmpty())
      continue;
    const QString expr(_instance.task().setenv().rawValue(key));
    /*Log::debug(_instance.task().id(), _instance.id())
        << "setting environment variable " << key << "="
        << expr << " " << _instance.params().keys(false).size() << " "
        << _instance.params().keys(true).size() << " ["
        << _instance.params().evaluate("%!yyyy %!taskid %{!taskid}", &_instance)
        << "]";*/
    key.replace(notIdentifier, "_");
    if (key[0] >= '0' && key[0] <= '9' )
      key.insert(0, '_');
    TaskInstancePseudoParamsProvider ppp(_instance);
    const QString value = _instance.params().evaluate(expr, &ppp);
    if (setenv)
      setenv->insert(key, value);
    sysenv->insert(key, value);
  }
}

void Executor::abort() {
  QMetaObject::invokeMethod(this, "doAbort");
}

void Executor::doAbort() {
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
    _process->kill();
  } else if (_reply) {
    Log::info(_instance.task().id(), _instance.idAsLong())
        << "http task abort requested";
    _reply->abort();
  } else if (_instance.task().mean() == Task::Workflow) {
    // TODO should abort running subtasks ? or let finishWorkflow do it ?
    finishWorkflow(false, -1);
  } else {
    Log::warning(_instance.task().id(), _instance.idAsLong())
        << "cannot abort task because its execution mean is not abortable";
  }
}

void Executor::taskInstanceFinishing(bool success, int returnCode) {
  _abortTimeout->stop();
  _instance.setSuccess(success);
  _instance.setReturnCode(returnCode);
  _instance.setEndDatetime();
  emit taskInstanceFinished(_instance, this);
  _instance = TaskInstance();
  _steps.clear(); // LATER give to TaskInstance for history ?
  foreach (TimerWithArguments *timer, _workflowTimers)
    delete timer;
  _workflowTimers.clear();}
