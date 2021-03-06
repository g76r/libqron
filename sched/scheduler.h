/* Copyright 2012-2017 Hallowyn and others.
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

class QThread;
class CronTrigger;

/** Core qron scheduler class.
 * Mainly responsible for configuration, queueing and event handling. */
class LIBQRONSHARED_EXPORT Scheduler : public QronConfigDocumentManager {
  Q_OBJECT
  Q_DISABLE_COPY(Scheduler)
  QThread *_thread;
  TaskInstanceList _queuedRequests;
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
  /** This method is threadsafe */
  void activateWorkflowTransition(
      TaskInstance workflowTaskInstance, WorkflowTransition transition,
      ParamSet eventContext);

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
  TaskInstanceList syncRequestTask(
      QString taskId, ParamSet paramsOverriding = ParamSet(),
      bool force = false, TaskInstance callerTask = TaskInstance());
  /** Explicitely request task execution now, but do not wait for validity
   * check of the request, therefore do not wait for Scheduler thread
   * processing the request.
   * If current thread is the Scheduler thread, the call is queued anyway.
   * @param taskId fully qualified task name, on the form "taskGroupId.taskId"
   * @param paramsOverriding override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @see syncRequestTask
   * @see RequestFormField */
  void asyncRequestTask(const QString taskId, ParamSet params = ParamSet(),
                        bool force = false,
                        TaskInstance callerTask = TaskInstance());
  /** Cancel a queued request.
   * @return TaskInstance.isNull() iff error (e.g. request not found or no
   * longer queued) */
  TaskInstance cancelRequest(quint64 id);
  TaskInstance cancelRequest(TaskInstance instance) {
    return cancelRequest(instance.idAsLong()); }
  /** Cancel all queued requests of a given task. */
  TaskInstanceList cancelRequestsByTaskId(QString taskId);
  /** @see cancelRequestsByTaskId(QString) */
  TaskInstanceList cancelRequestsByTaskId(Task task) {
    return cancelRequestsByTaskId(task.id()); }
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
  TaskInstance abortTask(quint64 id);
  /** @see abortTask(quint64) */
  TaskInstance abortTask(TaskInstance instance) {
    return abortTask(instance.idAsLong()); }
  /** Abort all running instance of a given task.
   * Same limitations than abortTask().
   * @see abortTask(quint64)
   */
  TaskInstanceList abortTaskInstancesByTaskId(QString taskId);
  /** @see abortTaskInstancesByTaskId(QString) */
  TaskInstanceList abortTaskInstancesByTaskId(Task task) {
    return abortTaskInstancesByTaskId(task.id()); }
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
  void reevaluateQueuedRequests();
  /** Enable or disable a task.
    * This method is threadsafe */
  bool enableTask(QString taskId, bool enable);
  /** Enable or disable all tasks at once.
    * This method is threadsafe */
  void enableAllTasks(bool enable);
  //LATER enableAllTasksWithinGroup
  /** Activate a new configuration. */
  void activateConfig(SchedulerConfig newConfig);
  // override config() to make it thread-safe
  /** Thread-safe (whereas QronConfigDocumentManager::config() is not) */
  SchedulerConfig config() {
    QMutexLocker ml(&_configGuard);
    return QronConfigDocumentManager::config(); }
  /** Thread-safe. Sorted in queue order. */
  TaskInstanceList queuedTaskInstances();
  /** Thread-safe. No order guarantee. */
  TaskInstanceList runningTaskInstances();
  /** Thread-safe. No order guarantee. */
  TaskInstanceList queuedOrRunningTaskInstances();

signals:
  void hostsResourcesAvailabilityChanged(
      QString host, QHash<QString,qint64> resources);
  void noticePosted(QString notice, ParamSet params);

private:
  void taskInstanceFinishing(TaskInstance instance, Executor *executor);
  void periodicChecks();
  /** Fire expired triggers for a given task. */
  Q_INVOKABLE void checkTriggersForTask(QVariant taskId);
  /** Fire expired triggers for all tasks. */
  Q_INVOKABLE void checkTriggersForAllTasks();
  void reloadAccessControlConfig();
  /** Reevaluate queued requests and start any task that can be started.
    * @see reevaluateQueuedRequests() */
  void startQueuedTasks();
  /** Check if it is permitted for a task to run now, if yes start it.
   * If instance.force() is true, start a task despite any constraint or limit,
   * even create a new (temporary) executor thread if needed.
   * @return true if the task was started or canceled */
  bool startQueuedTask(TaskInstance instance);
  /** @return true iff the triggers fires a task request */
  bool checkTrigger(CronTrigger trigger, Task task, QString taskId);
  void setTimerForCronTrigger(CronTrigger trigger, QDateTime previous
                              = QDateTime::currentDateTime());
  Q_INVOKABLE TaskInstanceList doRequestTask(
      QString taskId, ParamSet paramsOverriding, bool force,
      TaskInstance callerTask);
  TaskInstance enqueueRequest(TaskInstance request, ParamSet paramsOverriding);
  Q_INVOKABLE TaskInstance doCancelRequest(quint64 id);
  Q_INVOKABLE TaskInstanceList doCancelRequestsByTaskId(QString taskId);
  Q_INVOKABLE TaskInstanceList doAbortTaskInstancesByTaskId(QString taskId);
  Q_INVOKABLE TaskInstance doAbortTask(quint64 id);
  Q_INVOKABLE void doActivateWorkflowTransition(
      TaskInstance workflowTaskInstance, WorkflowTransition transition,
      ParamSet eventContext);
  void propagateTaskInstanceChange(TaskInstance instance);
  Q_INVOKABLE TaskInstanceList detachedQueuedTaskInstances();
  Q_INVOKABLE TaskInstanceList detachedRunningTaskInstances();
  Q_INVOKABLE TaskInstanceList detachedQueuedOrRunningTaskInstances();
};

#endif // SCHEDULER_H
