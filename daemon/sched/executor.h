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
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <QObject>
#include "data/taskrequest.h"
#include "data/host.h"
#include <QWeakPointer>
#include <QProcess>

class QThread;
class QNetworkAccessManager;
class QNetworkReply;

class Executor : public QObject {
  Q_OBJECT
  bool _isTemporary;
  QThread *_thread;
  QProcess *_process;
  QByteArray _errBuf;
  TaskRequest _request;
  Host _target;
  QNetworkAccessManager *_nam;

public:
  explicit Executor();
  void setTemporary(bool temporary = true) { _isTemporary = temporary; }
  bool isTemporary() const { return _isTemporary; }
  
public slots:
  /** Execute a request now.
    * This method is thread-safe.
    */
  void execute(TaskRequest request, Host target);

signals:
  void taskFinished(TaskRequest request, Host target, bool success,
                    int returnCode, QWeakPointer<Executor> executor);

private slots:
  void error(QProcess::ProcessError error);
  void finished(int exitCode, QProcess::ExitStatus exitStatus);
  void readyReadStandardError();
  void readyReadStandardOutput();
  void replyFinished(QNetworkReply *reply);

private:
  Q_INVOKABLE void doExecute(TaskRequest request, Host target);
  void execMean(TaskRequest request, Host target);
  void sshMean(TaskRequest request, Host target);
  void httpMean(TaskRequest request, Host target);
  void execProcess(TaskRequest request, Host target, QStringList cmdline);
  Q_DISABLE_COPY(Executor)
};

#endif // EXECUTOR_H
