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
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <QObject>
#include "taskinstance.h"
#include "config/host.h"
#include <QProcess>
#include <QNetworkReply>
#include <QTimer>

class QThread;
class QNetworkAccessManager;
class QNetworkReply;
class StepInstance;
class Alerter;

/** Class handling execution of a task after its being dequeued, from start
 * to end or cancellation. */
class LIBQRONSHARED_EXPORT Executor : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(Executor)
  bool _isTemporary, _stderrWasUsed;
  QThread *_thread;
  QProcess *_process;
  QByteArray _errBuf;
  TaskInstance _instance;
  QNetworkAccessManager *_nam;
  QProcessEnvironment _baseenv;
  QNetworkReply *_reply;
  Alerter *_alerter;
  QTimer *_abortTimeout;
  QHash<QString,StepInstance> _steps;
  QList<QTimer*> _workflowTimers;

public:
  explicit Executor(Alerter *alerter);
  ~Executor();
  void setTemporary(bool temporary = true) { _isTemporary = temporary; }
  bool isTemporary() const { return _isTemporary; }
  /** Execute a task now. This method is thread-safe. */
  void execute(TaskInstance instance);
  /** Abort current task now. This method is thread-safe. */
  void abort();
  /** This method is thread-safe. */
  void activateWorkflowTransition(WorkflowTransition transition,
                                  ParamSet eventContext);
  void noticePosted(QString notice, ParamSet params);

signals:
  /** There is no guarantee that taskStarted() is emited, taskFinished() can
    * be emited witout previous taskStarted(). */
  void taskInstanceStarted(TaskInstance instance);
  /** Signal emited whenever a task is no longer running or queued:
    * when finished on failure, finished on success, or cannot be started
    * because of a failure on start. */
  void taskInstanceFinished(TaskInstance instance, Executor *executor);

private slots:
  void processError(QProcess::ProcessError error);
  void processFinished(int exitCode, QProcess::ExitStatus exitStatus);
  void readyProcessWarningOutput();
  void readyReadStandardError();
  void readyReadStandardOutput();
  void replyError(QNetworkReply::NetworkError error);
  void replyFinished();
  void workflowCronTriggered(QVariant sourceLocalId);

private:
  void localMean();
  void sshMean();
  void httpMean();
  void workflowMean();
  void execProcess(QStringList cmdline, QProcessEnvironment sysenv);
  inline void prepareEnv(QProcessEnvironment *sysenv,
                         QHash<QString, QString> *setenv = 0);
  void replyHasFinished(QNetworkReply *reply,
                        QNetworkReply::NetworkError error);
  void taskInstanceFinishing(bool success, int returnCode);
  void finishWorkflow(bool success, int returnCode);
  void getReplyContent(QNetworkReply *reply, QString *replyContent,
                       QString maxsizeKey, QString maxwaitKey) const;
};

#endif // EXECUTOR_H
