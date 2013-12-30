/* Copyright 2012-2013 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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

Executor::Executor(Alerter *alerter) : QObject(0), _isTemporary(false),
  _stderrWasUsed(false), _thread(new QThread),
  _process(0), _nam(new QNetworkAccessManager(this)), _reply(0),
  _alerter(alerter), _abortTimeout(new QTimer(this)) {
  _thread->setObjectName(QString("Executor-%1")
                         .arg((long)_thread, sizeof(long)*2, 16,
                              QLatin1Char('0')));
  connect(this, SIGNAL(destroyed(QObject*)), _thread, SLOT(quit()));
  connect(_thread, SIGNAL(finished()), _thread, SLOT(deleteLater()));
  _thread->start();
  _baseenv = QProcessEnvironment::systemEnvironment();
  moveToThread(_thread);
  _abortTimeout->setSingleShot(true);
  connect(_abortTimeout, SIGNAL(timeout()), this, SLOT(doAbort()));
  //qDebug() << "creating new task executor" << this;
}

void Executor::execute(TaskInstance instance) {
  QMetaObject::invokeMethod(this, "doExecute", Q_ARG(TaskInstance, instance));
}

void Executor::doExecute(TaskInstance instance) {
  const QString mean = instance.task().mean();
  Log::debug(instance.task().fqtn(), instance.id())
      << "starting task '" << instance.task().fqtn() << "' through mean '"
      << mean << "' after " << instance.queuedMillis() << " ms in queue";
  _instance = instance;
  _stderrWasUsed = false;
  long long maxDurationBeforeAbort = instance.task().maxDurationBeforeAbort();
  if (maxDurationBeforeAbort <= INT_MAX)
    _abortTimeout->start(maxDurationBeforeAbort);
  if (mean == "local")
    localMean(instance);
  else if (mean == "ssh")
    sshMean(instance);
  else if (mean == "http")
    httpMean(instance);
  else if (mean == "donothing") {
    emit taskStarted(instance);
    taskFinishing(true, 0);
  } else {
    Log::error(instance.task().fqtn(), instance.id())
        << "cannot execute task with unknown mean '" << mean << "'";
    taskFinishing(false, -1);
  }
}

void Executor::localMean(TaskInstance instance) {
  QStringList cmdline;
  cmdline = instance.params()
      .splitAndEvaluate(instance.command(), &instance);
  Log::info(instance.task().fqtn(), instance.id())
      << "exact command line to be executed (locally): " << cmdline.join(" ");
  QProcessEnvironment sysenv;
  prepareEnv(instance, &sysenv);
  instance.setAbortable();
  execProcess(instance, cmdline, sysenv);
}

void Executor::sshMean(TaskInstance instance) {
  QStringList cmdline, sshCmdline;
  cmdline = instance.params()
      .splitAndEvaluate(instance.command(), &instance);
  Log::info(instance.task().fqtn(), instance.id())
      << "exact command line to be executed (through ssh on host "
      << instance.target().hostname() <<  "): " << cmdline.join(" ");
  QHash<QString,QString> setenv;
  QProcessEnvironment sysenv;
  prepareEnv(instance, &sysenv, &setenv);
  QString username = instance.params().value("ssh.username");
  qlonglong port = instance.params().valueAsLong("ssh.port");
  QString ignoreknownhosts = instance.params().value("ssh.ignoreknownhosts",
                                                    "true");
  QString identity = instance.params().value("ssh.identity");
  QStringList options = instance.params().valueAsStrings("ssh.options");
  bool disablepty = instance.params().value("ssh.disablepty") == "true";
  sshCmdline << "ssh" << "-oLogLevel=ERROR" << "-oEscapeChar=none"
             << "-oServerAliveInterval=10" << "-oServerAliveCountMax=3"
             << "-oIdentitiesOnly=yes" << "-oKbdInteractiveAuthentication=no"
             << "-oBatchMode=yes" << "-oConnectionAttempts=3"
             << "-oTCPKeepAlive=yes" << "-oPasswordAuthentication=false";
  if (!disablepty) {
    sshCmdline << "-t" << "-t";
    instance.setAbortable();
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
  sshCmdline << instance.target().hostname();
  foreach (QString key, setenv.keys())
    if (!instance.task().unsetenv().contains(key)) {
      QString value = setenv.value(key);
      value.replace('\'', QString());
      sshCmdline << key+"='"+value+"'";
    }
  sshCmdline << cmdline;
  execProcess(instance, sshCmdline, sysenv);
}

void Executor::execProcess(TaskInstance instance, QStringList cmdline,
                           QProcessEnvironment sysenv) {
  if (cmdline.isEmpty()) {
    Log::warning(instance.task().fqtn(), instance.id())
        << "cannot execute task with empty command '"
        << instance.task().fqtn() << "'";
    taskFinishing(false, -1);
    return;
  }
  _errBuf.clear();
  _process = new QProcess(this);
  _process->setProcessChannelMode(QProcess::SeparateChannels);
  connect(_process, SIGNAL(error(QProcess::ProcessError)),
          this, SLOT(processError(QProcess::ProcessError)));
  connect(_process, SIGNAL(finished(int,QProcess::ExitStatus)),
          this, SLOT(processFinished(int,QProcess::ExitStatus)));
  connect(_process, SIGNAL(readyReadStandardError()),
          this, SLOT(readyReadStandardError()));
  connect(_process, SIGNAL(readyReadStandardOutput()),
          this, SLOT(readyReadStandardOutput()));
  _process->setProcessEnvironment(sysenv);
  QString program = cmdline.takeFirst();
  Log::debug(_instance.task().fqtn(), _instance.id())
      << "about to start system process '" << program << "' with args "
      << cmdline << " and environment " << sysenv.toStringList();
  emit taskStarted(instance);
  _process->start(program, cmdline);
}

void Executor::processError(QProcess::ProcessError error) {
  //qDebug() << "************ processError" << _instance.id() << _process;
  if (!_process)
    return; // LATER add log
  readyReadStandardError();
  readyReadStandardOutput();
  Log::warning(_instance.task().fqtn(), _instance.id()) // TODO info if aborting
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
  Log::log(success ? Log::Info : Log::Warning, _instance.task().fqtn(),
           _instance.id())
      << "task '" << _instance.task().fqtn() << "' finished "
      << (success ? "successfully" : "in failure") << " with return code "
      << exitCode << " on host '" << _instance.target().hostname() << "' in "
      << _instance.runningMillis() << " ms";
  if (!_stderrWasUsed  && _alerter)
    _alerter->cancelAlert("task.stderr."+_instance.task().fqtn());
  /* Qt doc is not explicit if delete should only be done when
     * QProcess::finished() is emited, but we get here too when
     * QProcess::error() is emited.
     * In the other hand it is not sure that finished() is always emited
     * after an error(), may be in some case error() can be emited alone. */
  _process->deleteLater();
  _process = 0;
  _errBuf.clear();
  taskFinishing(success, exitCode);
}

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
        static QRegExp sshConnClosed("^Connection to [^ ]* closed\\.$");
        QList<QRegExp> filters(_instance.task().stderrFilters());
        if (filters.isEmpty() && _instance.task().mean() == "ssh")
          filters.append(sshConnClosed);
        foreach (QRegExp filter, filters)
          if (filter.indexIn(line) >= 0)
            goto line_filtered;
        Log::warning(_instance.task().fqtn(), _instance.id())
            << "task stderr: " << line;
        if (!_stderrWasUsed) {
          _stderrWasUsed = true;
          if (_alerter && !_instance.task().params()
              .valueAsBool("disable.alert.stderr", false))
            _alerter->raiseAlert("task.stderr."+_instance.task().fqtn());
        }
line_filtered:;
      }
    }
  }
}

void Executor::readyReadStandardError() {
  //qDebug() << "************ readyReadStandardError" << _instance.task().fqtn() << _instance.id() << _process;
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardError);
  readyProcessWarningOutput();
}

void Executor::readyReadStandardOutput() {
  //qDebug() << "************ readyReadStandardOutput" << _instance.task().fqtn() << _instance.id() << _process;
  if (!_process)
    return;
  _process->setReadChannel(QProcess::StandardOutput);
  if (_instance.task().mean() == "ssh"
      && _instance.params().value("ssh.disablepty") != "true")
    readyProcessWarningOutput(); // with pty, stderr and stdout are merged
  else
    while (!_process->read(1024).isEmpty());
  // LATER make it possible to log stdout too (as debug, depending on task cfg)
}

void Executor::httpMean(TaskInstance instance) {
  // LATER http mean should support http auth, http proxy auth and ssl
  QString method = instance.params().value("method");
  QUrl url;
  int port = instance.params().valueAsInt("port", 80);
  QString hostname = instance.params()
      .evaluate(instance.target().hostname(), &instance);
  QString command = instance.params()
      .evaluate(instance.command(), &instance);
  if (command.size() && command.at(0) == '/')
    command = command.mid(1);
  url.setUrl(QString("http://%1:%2/%3").arg(hostname).arg(port).arg(command),
             QUrl::TolerantMode);
  QNetworkRequest networkRequest(url);
  foreach (QString name, instance.setenv().keys()) {
    const QString expr(instance.setenv().rawValue(name));
    if (name.endsWith(":")) // ignoring : at end of header name
      name.chop(1);
    name.replace(QRegExp("[^a-zA-Z_0-9\\-]+"), "_");
    const QString value = instance.params().evaluate(expr, &instance);
    //Log::fatal(instance.task().fqtn(), instance.id()) << "setheader: " << name << "=" << value << ".";
    networkRequest.setRawHeader(name.toLatin1(), value.toUtf8());
  }
  // LATER read request output, at less to avoid server being blocked and request never finish
  if (url.isValid()) {
    instance.setAbortable();
    emit taskStarted(instance);
    if (method.isEmpty() || method.compare("get", Qt::CaseInsensitive) == 0) {
      Log::info(_instance.task().fqtn(), _instance.id())
          << "exact GET URL to be called: "
          << url.toString(QUrl::RemovePassword);
      _reply = _nam->get(networkRequest);
    } else if (method.compare("put", Qt::CaseInsensitive) == 0) {
      Log::info(_instance.task().fqtn(), _instance.id())
          << "exact PUT URL to be called: "
          << url.toString(QUrl::RemovePassword);
      _reply = _nam->put(networkRequest, QByteArray());
    } else if (method.compare("post", Qt::CaseInsensitive) == 0) {
      Log::info(_instance.task().fqtn(), _instance.id())
          << "exact POST URL to be called: "
          << url.toString(QUrl::RemovePassword);
      networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
      _reply = _nam->post(networkRequest, QByteArray());
    } else {
      Log::error(_instance.task().fqtn(), _instance.id())
          << "unsupported HTTP method: " << method;
      taskFinishing(false, -1);
    }
    if (_reply) {
      // note that the apparent critical window between QNAM::get/put/post()
      // and connection to reply signals is not actually critical since
      // QNetworkReply lies in the same thread than QNAM and Executor, and
      // therefore no QNetworkReply slot can executed meanwhile hence no
      // QNetworkReply::finished() cannot be emitted before connection
      // FIXME is connection to error() usefull ? can error() be emited w/o finished() ?
      connect(_reply, SIGNAL(error(QNetworkReply::NetworkError)),
              this, SLOT(replyError(QNetworkReply::NetworkError)));
      connect(_reply, SIGNAL(finished()), this, SLOT(replyFinished()));
    }
  } else {
    Log::error(_instance.task().fqtn(), _instance.id())
        << "unsupported HTTP URL: " << url.toString(QUrl::RemovePassword);
    taskFinishing(false, -1);
  }
}

void Executor::replyError(QNetworkReply::NetworkError error) {
  replyHasFinished(qobject_cast<QNetworkReply*>(sender()), error);
}

void Executor::replyFinished() {
  replyHasFinished(qobject_cast<QNetworkReply*>(sender()),
                  QNetworkReply::NoError);
}

// executed by the first emitted signal among QNetworkReply::error() and
// QNetworkReply::finished(), therefore it can be called twice when an error
// occurs (in fact most of the time where an error occurs, but in cases where
// error() is not followed by finished() and I am not sure there are such cases)
void Executor::replyHasFinished(QNetworkReply *reply,
                               QNetworkReply::NetworkError error) {
  QString fqtn(_instance.task().fqtn());
  if (!_reply) {
    Log::debug() << "Executor::replyFinished called as it is not responsible "
                    "of any http request";
    // seems normal on some network error ?
    return;
  }
  if (!reply) {
    Log::error(fqtn, _instance.id())
        << "Executor::replyFinished receive null pointer";
    return;
  }
  if (reply != _reply) {
    Log::error(fqtn, _instance.id())
        << "Executor::replyFinished receive unrelated pointer";
    return;
  }
  int status = reply
      ->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QString reason = reply
      ->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
  bool success;
  if (error == QNetworkReply::NoError) {
    success =  status >= 200 && status <= 299;
    success = _instance.task().params()
        .valueAsBool("return.code.default.success", success);
    success = _instance.task().params()
        .valueAsBool("return.code."+QString::number(status)+".success",success);
  } else
    success = false;
  _instance.setEndDatetime();
  Log::log(success ? Log::Info : Log::Warning, fqtn, _instance.id())
      << "task '" << fqtn << "' finished "
      << (success ? "successfully" : "in failure") << " with return code "
      << status << " (" << reason << ") on host '"
      << _instance.target().hostname() << "' in " << _instance.runningMillis()
      << " ms, with network error '" << networkErrorAsString(error)
      << "' (code " << error << ")";
  // LATER translate network error codes into human readable strings
  reply->deleteLater();
  _reply = 0;
  taskFinishing(success, status);
}

void Executor::prepareEnv(TaskInstance instance, QProcessEnvironment *sysenv,
                          QHash<QString,QString> *setenv) {
  if (instance.task().params().valueAsBool("clearsysenv"))
    *sysenv = QProcessEnvironment();
  else
    *sysenv = _baseenv;
  // first clean system base env from any unset variables
  foreach (const QString pattern, instance.task().unsetenv().keys()) {
    QRegExp re(pattern, Qt::CaseInsensitive, QRegExp::WildcardUnix);
    foreach (const QString key, sysenv->keys())
      if (re.exactMatch(key))
        sysenv->remove(key);
  }
  // then build setenv evaluated paramset that may be used apart from merging
  // into sysenv
  foreach (QString key, instance.setenv().keys()) {
    const QString expr(instance.setenv().rawValue(key));
    /*Log::debug(instance.task().fqtn(), instance.id())
        << "setting environment variable " << key << "="
        << expr << " " << instance.params().keys(false).size() << " "
        << instance.params().keys(true).size() << " ["
        << instance.params().evaluate("%!yyyy %!fqtn %{!fqtn}", &_instance)
        << "]";*/
    static QRegExp notIdentifier("[^a-zA-Z_0-9]+");
    key.replace(QRegExp(notIdentifier), "_");
    if (key.size() > 0 && strchr("0123456789", key[0].toLatin1()))
      key.insert(0, '_');
    const QString value = instance.params().evaluate(expr, &instance);
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
    if (_instance.task().mean() == "ssh")
      Log::warning(_instance.task().fqtn(), _instance.id())
          << "cannot abort task because ssh tasks are not abortable when "
             "ssh.disablepty is set to true";
    else
      Log::warning(_instance.task().fqtn(), _instance.id())
          << "cannot abort task because it is marked as not abortable";
  } else if (_process) {
    Log::info(_instance.task().fqtn(), _instance.id())
        << "process task abort requested";
    _process->kill();
  } else if (_reply) {
    Log::info(_instance.task().fqtn(), _instance.id())
        << "http task abort requested";
    _reply->abort();
  } else {
    Log::warning(_instance.task().fqtn(), _instance.id())
        << "cannot abort task because its execution mean is not abortable";
  }
}

void Executor::taskFinishing(bool success, int returnCode) {
  _abortTimeout->stop();
  _instance.setSuccess(success);
  _instance.setReturnCode(returnCode);
  _instance.setEndDatetime();
  emit taskFinished(_instance, this);
  _instance = TaskInstance();
}
