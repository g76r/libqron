/* Copyright 2012-2022 Hallowyn and others.
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
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QObject>
#include <QSet>
#include <QIODevice>
#include "util/paramset.h"
#include "config/taskgroup.h"
#include "config/task.h"
#include "config/host.h"
#include "config/cluster.h"
#include "sched/taskinstance.h"
#include <QHash>
#include "executor.h"
#include <QVariant>
#include "alert/alerter.h"
#include "config/eventsubscription.h"
#include "pf/pfnode.h"
#include "auth/inmemoryauthenticator.h"
#include "auth/inmemoryusersdatabase.h"
#include <QFileSystemWatcher>
#include "config/logfile.h"
#include "config/qronconfigdocumentmanager.h"
#include <random>
#include <QDeadlineTimer>

class QThread;
class CronTrigger;

/** Core qron scheduler class.
 * Mainly responsible for configuration, queueing and event handling. */
class LIBQRONSHARED_EXPORT Scheduler : public QronConfigDocumentManager {
  Q_OBJECT
  Q_DISABLE_COPY(Scheduler)
  QThread *_thread;
  TaskInstanceList _queuedTasks;
  QHash<quint64,TaskInstance> _unfinishedTasks;
  QHash<TaskInstance,TaskInstanceList> _waitingTasks;
  QHash<TaskInstance,Executor*> _runningTasks;
  QList<Executor*> _availableExecutors;
  Alerter *_alerter;
  InMemoryAuthenticator *_authenticator;
  InMemoryUsersDatabase *_usersDatabase;
  bool _firstConfigurationLoad;
  qint64 _startdate, _configdate;
  qint64 _execCount, _runningTasksHwm, _queuedTasksHwm;
  QFileSystemWatcher *_accessControlFilesWatcher;
  PfNode _accessControlNode;
  QHash<QString, QHash<QString,qint64>> _consumedResources; // <host,<resource,quantity>>
  std::random_device _randomDevice;
  std::mt19937 _uniformRandomNumberGenerator;
  QMutex _configGuard;
  bool _shutingDown = false;

public:
  Scheduler();
  ~Scheduler();
  void customEvent(QEvent *event);
  Alerter *alerter() { return _alerter; }
  Authenticator *authenticator() { return _authenticator; }
  UsersDatabase *usersDatabase() { return _usersDatabase; }
  QDateTime startdate() const {
    return QDateTime::fromMSecsSinceEpoch(_startdate); }
  QDateTime configdate() const {
    return _configdate == LLONG_MIN
        ? QDateTime() : QDateTime::fromMSecsSinceEpoch(_configdate); }
  qint64 execCount() const { return _execCount; }
  qint64 runningTasksHwm() const { return _runningTasksHwm; }
  qint64 queuedTasksHwm() const { return _queuedTasksHwm; }

public slots:
  /** Explicitely request task execution now.
   * This method will block current thread until the request is either
   * queued either denied by Scheduler thread.
   * If current thread is the Scheduler thread, the method is a direct call.
   * @param taskId fully qualified task name, on the form "taskGroupId.taskId"
   * @param paramsOverriding override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @return isEmpty() if task cannot be queued
   * @see asyncRequestTask
   * @see RequestFormField */
  TaskInstanceList syncRequestTask(QString taskId, ParamSet params = ParamSet(),
      bool force = false, TaskInstance herder = TaskInstance());
  TaskInstanceList syncRequestTask(
      QString taskId, ParamSet params, bool force, QString herdId);
  /** Explicitely request task execution now, but do not wait for validity
   * check of the request, therefore do not wait for Scheduler thread
   * processing the request.
   * If current thread is the Scheduler thread, the call is queued anyway.
   * @param taskId fully qualified task name, on the form "taskGroupId.taskId"
   * @param paramsOverriding override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @see syncRequestTask
   * @see RequestFormField */
  void asyncRequestTask(
      const QString taskId, ParamSet params = ParamSet(), bool force = false,
      TaskInstance herder = TaskInstance());
  void asyncRequestTask(
      const QString taskId, ParamSet params, bool force, QString herdId);
  /** Cancel a queued request.
   * @return TaskInstance.isNull() iff error (e.g. request not found or no
   * longer queued) */
  TaskInstance cancelTaskInstance(quint64 id);
  TaskInstance cancelTaskInstance(TaskInstance instance) {
    return cancelTaskInstance(instance.idAsLong()); }
  /** Cancel all queued requests of a given task. */
  TaskInstanceList cancelTaskInstancesByTaskId(QString taskId);
  /** @see cancelRequestsByTaskId(QString) */
  TaskInstanceList cancelTaskInstancesByTaskId(Task task) {
    return cancelTaskInstancesByTaskId(task.id()); }
  /** Abort a running task instance.
   * For local tasks aborting means killing, for ssh tasks aborting means
   * killing ssh client hence most of time killing actual task, for http tasks
   * aborting means closing the socket.
   * Beware that, but for local tasks, aborting a task does not guarantees that
   * the application processing is actually ended whereas it frees resources
   * and tasks instance counters, hence enabling immediate reexecution of the
   * same task.
   * @return TaskInstance.isNull() iff error (e.g. task instance not found or no
   * longer running) */
  TaskInstance abortTaskInstance(quint64 id);
  /** @see abortTask(quint64) */
  TaskInstance abortTaskInstance(TaskInstance instance) {
    return abortTaskInstance(instance.idAsLong()); }
  /** Abort all running instance of a given task.
   * Same limitations than abortTask().
   * @see abortTask(quint64)
   */
  TaskInstanceList abortTaskInstanceByTaskId(QString taskId);
  /** @see abortTaskInstancesByTaskId(QString) */
  TaskInstanceList abortTaskInstanceByTaskId(Task task) {
    return abortTaskInstanceByTaskId(task.id()); }
  /** Post a notice.
   * This method is thread-safe.
   * If params has no parent it will be set global params as parent */
  void postNotice(QString notice, ParamSet params);
  /** Ask for queued requests to be reevaluated during next event loop
    * iteration.
    * This method must be called every time something occurs that could make a
    * queued task runnable. Calling this method several time within the same
    * event loop iteration will trigger reevaluation only once (same pattern as
    * QWidget::update()). */
  void reevaluateQueuedTaskInstances();
  /** Enable or disable a task.
    * This method is threadsafe */
  bool enableTask(QString taskId, bool enable);
  /** Enable or disable all tasks at once.
    * This method is threadsafe */
  void enableAllTasks(bool enable);
  //LATER enableAllTasksWithinGroup
  /** Activate a new configuration. */
  void activateConfig(SchedulerConfig newConfig);
  /** Shutdown scheduler: stop starting tasks and wait for those already running
   * until deadline is reached). */
  void shutdown(QDeadlineTimer deadline = QDeadlineTimer::Forever);

public:
  // override config() to make it thread-safe
  /** Thread-safe (whereas QronConfigDocumentManager::config() is not) */
  SchedulerConfig config() {
    QMutexLocker ml(&_configGuard);
    return QronConfigDocumentManager::config(); }
  /** Thread-safe. No order guarantee. */
  QHash<quint64, TaskInstance> unfinishedTaskInstances();

signals:
  void hostsResourcesAvailabilityChanged(
      QString host, QHash<QString,qint64> resources);
  void noticePosted(QString notice, ParamSet params);

private:
  void taskInstanceStoppedOrCanceled(TaskInstance instance, Executor *executor,
      bool processCanceledAsFailure);
  void taskInstanceFinishedOrCanceled(
      TaskInstance instance, bool processCanceledAsFailure);
  void periodicChecks();
  /** Fire expired triggers for a given task. */
  void checkTriggersForTask(QVariant taskId);
  /** Fire expired triggers for all tasks. */
  void checkTriggersForAllTasks();
  void reloadAccessControlConfig();
  /** Reevaluate queued requests and start any task that can be started.
    * @see reevaluateQueuedRequests() */
  void startAsManyTaskInstancesAsPossible();
  /** Check if it is permitted for a task to run now, if yes start it.
   * If instance.force() is true, start a task despite any constraint or limit,
   * even create a new (temporary) executor thread if needed.
   * @return true if the task was started or canceled */
  bool startTaskInstance(TaskInstance instance);
  /** @return true iff the triggers fires a task request */
  bool checkTrigger(CronTrigger trigger, Task task, QString taskId);
  void setTimerForCronTrigger(CronTrigger trigger, QDateTime previous
                              = QDateTime::currentDateTime());
  TaskInstanceList doRequestTask(
      QString taskId, ParamSet params, bool force, TaskInstance herder);
  TaskInstanceList doRequestTask(
      QString taskId, ParamSet params, bool force, QString herdId);
  TaskInstance enqueueTaskInstance(TaskInstance request, ParamSet params);
  TaskInstance doCancelTaskInstance(
      TaskInstance instance, bool warning, const char *reason);
  TaskInstance doCancelTaskInstance(
      TaskInstance instance, bool warning, QString reason) {
    return doCancelTaskInstance(instance, warning, reason.toUtf8().constData()); }
  TaskInstanceList doCancelTaskInstancesByTaskId(QString taskId);
  TaskInstanceList doAbortTaskInstanceByTaskId(QString taskId);
  TaskInstance doAbortTaskInstance(quint64 id);
  void propagateTaskInstanceChange(TaskInstance instance);
  QHash<quint64, TaskInstance> detachedUnfinishedTaskInstances();
  void doShutdown(QDeadlineTimer deadline);
  void triggerStartActions(TaskInstance instance);
  void triggerFinishActions(
      TaskInstance instance, std::function<bool(Action)> filter);
};

#endif // SCHEDULER_H
