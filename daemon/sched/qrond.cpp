/* Copyright 2013 Hallowyn and others.
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
#include "qrond.h"
#include <QCoreApplication>
#include <QFile>
#include <QtDebug>
#include "log/log.h"
#include "log/filelogger.h"
#include <QThread>
#include <unistd.h>
#ifdef __unix__
#include <signal.h>
#endif

Q_GLOBAL_STATIC(Qrond, qrondInstance)

Qrond::Qrond(QObject *parent) : QObject(parent),
  _webconsoleAddress(QHostAddress::Any), _webconsolePort(8086),
  _scheduler(new Scheduler),
  _httpd(new HttpServer(8, 32)), // LATER should be configurable
  _webconsole(new WebConsole), _configPath("/etc/qron.conf") {
  _webconsole->setScheduler(_scheduler);
  _httpd->appendHandler(_webconsole);
}

Qrond::~Qrond() {
  // HttpServer and Scheduler will be deleted, but their threads won't since
  // they lies in main thread which event loop is no longer running
  // WebConsole will be deleted since HttpServer connects its deleteLater()
  _httpd->deleteLater();
  _scheduler->deleteLater();
}

void Qrond::startup(QStringList args) {
  int n = args.size();
  for (int i =0; i < n; ++i) {
    const QString &arg = args[i];
    if (i < n-1 && arg == "-l") {
      _webconsoleAddress.setAddress(args[++i]);
    } else if (i < n-1 && arg == "-p") {
      int p = QString(args[++i]).toInt();
      if (p >= 1 && p <= 65535)
        _webconsolePort = p;
      else
        Log::error() << "bad port number: " << arg
                     << " using default instead (8086)";
    } else if (i < n-1 && arg == "-c") {
       _configPath = args[++i];
    } else {
      Log::warning() << "unknown command line parameter: " << arg;
    }
  }
  if (!_httpd->listen(_webconsoleAddress, _webconsolePort))
    Log::error() << "cannot start webconsole on "
                 << _webconsoleAddress.toString() << ":" << _webconsolePort
                 << ": " << _httpd->errorString();
  QFile *config = new QFile(_configPath);
  // LATER have a config directory rather only one file
  QString errorString;
  int rc = _scheduler->reloadConfiguration(config, errorString) ? 0 : 1;
  delete config;
  if (rc) {
    Log::fatal() << "cannot load configuration: " << errorString;
    Log::fatal() << "qrond is aborting startup sequence";
    shutdown(rc);
  }
}

void Qrond::reload() {
  QString errorString;
  QFile *config = new QFile(_configPath);
  // LATER have a config directory rather only one file
  Log::info() << "reloading configuration";
  int rc = _scheduler->reloadConfiguration(config, errorString) ? 0 : 1;
  delete config;
  if (rc)
    Log::error() << "cannot reload configuration: " << errorString;
}

void Qrond::shutdown(int returnCode) {
  Log::info() << "qrond is shuting down";
  // TODO wait for running tasks while starting new ones is disabled
  QThread::currentThread()->exit(returnCode);
}

#ifdef __unix__
static void signal_handler(int signal_number) {
  //qDebug() << "signal" << signal_number;
  switch (signal_number) {
  case SIGHUP:
    QMetaObject::invokeMethod(qrondInstance(), "reload", Qt::QueuedConnection);
    break;
  case SIGTERM:
  case SIGINT:
    QMetaObject::invokeMethod(qrondInstance(), "shutdown",
                              Qt::QueuedConnection, Q_ARG(int, 0));
    break;
  }
}
#endif

int main(int argc, char *argv[]) {
  QCoreApplication a(argc, argv);
  QThread::currentThread()->setObjectName("MainThread");
  Log::addConsoleLogger(Log::Fatal);
  QStringList args;
  for (int i = 1; i < argc; ++i)
    args << QString::fromUtf8(argv[i]);
  qrondInstance()->startup(args);
#ifdef __unix__
  struct sigaction action;
  memset(&action, 0, sizeof(struct sigaction));
  action.sa_handler = signal_handler;
  sigaction(SIGHUP, &action, 0);
  sigaction(SIGTERM, &action, 0);
  sigaction(SIGINT, &action, 0);
#endif
  // LATER truly daemonize on Unix (pidfile...)
  // LATER servicize on Windows
  int rc = a.exec();
#ifdef __unix__
  action.sa_handler = SIG_IGN;
  sigaction(SIGHUP, &action, 0);
  sigaction(SIGTERM, &action, 0);
  sigaction(SIGINT, &action, 0);
#endif
  ::usleep(100000); // give a chance for last asynchronous log writing
  Log::clearLoggers(); // this deletes logger and console
  return rc;
}