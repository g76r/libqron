/* Copyright 2012-2023 Hallowyn and others.
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

#include "config/qronconfigdocumentmanager.h"
#include "taskinstance.h"
#include "pf/pfnode.h"
#include <random>
#include <QDeadlineTimer>
#include "condition/condition.h"

class QThread;
class QFileSystemWatcher;
class CronTrigger;
class Executor;
class Alerter;
class Authenticator;
class InMemoryAuthenticator;
class UsersDatabase;
class InMemoryUsersDatabase;

/** Core qron scheduler class.
 * Mainly responsible for configuration, queueing and event handling. */
class LIBQRONSHARED_EXPORT Scheduler : public QronConfigDocumentManager {
  Q_OBJECT
  Q_DISABLE_COPY(Scheduler)
  QThread *_thread;
  QMap<quint64,TaskInstance> _unfinishedTasks; // task instances repository
  QMap<quint64,QSet<quint64>> _unfinishedHerds; // herders -> herded tasks
  QMap<quint64,QSet<quint64>> _awaitedTasks; // herders waiting lists
  QHash<quint64,Executor*> _runningExecutors;
  QSet<quint64> _dirtyHerds; // for which planned tasks must be reevaluated
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
  Authenticator *authenticator();
  UsersDatabase *usersDatabase();
  QDateTime startdate() const {
    return QDateTime::fromMSecsSinceEpoch(_startdate); }
  QDateTime configdate() const {
    return _configdate == LLONG_MIN
        ? QDateTime() : QDateTime::fromMSecsSinceEpoch(_configdate); }
  qint64 execCount() const { return _execCount; }
  qint64 runningTasksHwm() const { return _runningTasksHwm; }
  qint64 queuedTasksHwm() const { return _queuedTasksHwm; }

public slots:
  /** Request task execution as soon as possible (create and queue it for
   * execution).
   * This method is thread-safe.
   * This method will block current thread until the request is either
   * queued either denied by Scheduler thread.
   * @param taskId fully qualified task name, on the form "taskGroupId.taskId"
   * @param params override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @return isEmpty() if task cannot be queued
   * @see RequestFormField */
  TaskInstanceList requestTask(
    QByteArray taskId, ParamSet overridingParams, bool force = false,
    quint64 herdid = 0);
  TaskInstanceList requestTask(
    QString taskId, ParamSet overridingParams, bool force = false,
      quint64 herdid = 0) {
    return requestTask(taskId.toUtf8(), overridingParams, force, herdid); }
  /** Plan task execution with conditions that must be met to queue it or
   * cancel it.
   * This method is thread-safe.
   * This method will block current thread until the request is either
   * queued either denied by Scheduler thread. */
  TaskInstanceList planTask(QByteArray taskId, ParamSet overridingParams,
                            bool force, quint64 herdid,
                            Condition queuewhen, Condition cancelwhen);
  TaskInstanceList planTask(QString taskId, ParamSet overridingParams,
                            bool force, quint64 herdid,
                            Condition queuewhen, Condition cancelwhen) {
    return planTask(taskId.toUtf8(), overridingParams, force, herdid, queuewhen,
                    cancelwhen); }
  /** Cancel a planned or queued request.
   * @return TaskInstance.isNull() iff error (e.g. request not found or no
   * longer queued) */
  TaskInstance cancelTaskInstance(quint64 id);
  TaskInstance cancelTaskInstance(TaskInstance instance) {
    return cancelTaskInstance(instance.idAsLong()); }
  /** Cancel all planned or queued requests of a given task. */
  TaskInstanceList cancelTaskInstancesByTaskId(QByteArray taskId);
  /** @see cancelRequestsByTaskId(QByteArray) */
  TaskInstanceList cancelTaskInstancesByTaskId(QString taskId) {
    return cancelTaskInstancesByTaskId(taskId.toUtf8()); }
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
  TaskInstanceList abortTaskInstanceByTaskId(QByteArray taskId);
  /** @see abortTaskInstancesByTaskId(QByteArray) */
  TaskInstanceList abortTaskInstanceByTaskId(QString taskId) {
    return abortTaskInstanceByTaskId(taskId.toUtf8()); }
  /** Post a notice.
   * This method is thread-safe.
   * If params has no parent it will be set global params as parent */
  void postNotice(QByteArray notice, ParamSet params);
  void postNotice(QString notice, ParamSet params) {
    postNotice(notice.toUtf8(), params); }
  /** Ask for queued requests to be reevaluated during next event loop
    * iteration.
    * This method must be called every time something occurs that could make a
    * queued task runnable. Calling this method several time within the same
    * event loop iteration will trigger reevaluation only once (same pattern as
    * QWidget::update()). */
  void reevaluateQueuedTaskInstances();
  /** Enable or disable a task.
    * This method is threadsafe */
  bool enableTask(QByteArray taskId, bool enable);
  bool enableTask(QString taskId, bool enable) {
    return enableTask(taskId.toUtf8(), enable); }
  /** Enable or disable all tasks at once.
    * This method is threadsafe */
  void enableAllTasks(bool enable);
  //LATER enableAllTasksWithinGroup
  /** Activate a new configuration. */
  void activateConfig(SchedulerConfig newConfig);
  /** Shutdown scheduler: stop starting tasks and wait for those already running
   * until deadline is reached). */
  void shutdown(QDeadlineTimer deadline = QDeadlineTimer::Forever);
  /** Either set param if empty or append a space followed by value to current
   * value.
   * Thread-safe asynchronous call (won't wait for the param being appended,
   * won't guarantee any order if several calls are made in the same time). */
  void taskInstanceParamAppend(quint64 taskinstanceid, QString key,
                               QString value);

public:
  // override config() to make it thread-safe
  /** Thread-safe (whereas QronConfigDocumentManager::config() is not) */
  SchedulerConfig config() {
    QMutexLocker ml(&_configGuard);
    return QronConfigDocumentManager::config(); }
  /** Thread-safe. */
  QMap<quint64,TaskInstance> unfinishedTaskInstances();

signals:
  void hostsResourcesAvailabilityChanged(
      QString host, QHash<QString,qint64> resources);
  void noticePosted(QByteArray notice, ParamSet params);

private:
  void taskInstanceStoppedOrCanceled(TaskInstance instance, Executor *executor,
      bool processCanceledAsFailure);
  void taskInstanceFinishedOrCanceled(
      TaskInstance instance, bool processCanceledAsFailure);
  void periodicChecks();
  /** Fire expired triggers for a given task. */
  void checkTriggersForTask(QByteArray taskId);
  /** Fire expired triggers for all tasks. */
  void checkTriggersForAllTasks();
  void reloadAccessControlConfig();
  /** Reevaluate queued instances  and start any task that can be started.
    * @see reevaluateQueuedRequests() */
  void startAsManyTaskInstancesAsPossible();
  /** Reevaluate planned instances and start any task that can be started. */
  void reevaluatePlannedTaskInstancesForHerd(quint64 herdid);
  void enqueueAsManyTaskInstancesAsPossible();
  /** Check if it is permitted for a task to run now, if yes start it.
   * If instance.force() is true, start a task despite any constraint or limit,
   * even create a new (temporary) executor thread if needed. */
  void startTaskInstance(TaskInstance instance);
  /** @return true iff the triggers fires a task request */
  bool checkTrigger(CronTrigger trigger, Task task, QByteArray taskId);
  void setTimerForCronTrigger(CronTrigger trigger, QDateTime previous
                              = QDateTime::currentDateTime());
  TaskInstanceList doRequestTask(
      QByteArray taskId, ParamSet overridingParams, bool force, quint64 herdid);
  TaskInstanceList doPlanTask(
      QByteArray taskId, ParamSet overridingParams, bool force, quint64 herdid,
      Condition queuewhen, Condition cancelwhen);
  TaskInstance enqueueTaskInstance(TaskInstance request);
  TaskInstance doCancelTaskInstance(
      TaskInstance instance, bool warning, const char *reason);
  TaskInstance doCancelTaskInstance(
      TaskInstance instance, bool warning, QString reason) {
    return doCancelTaskInstance(instance, warning, reason.toUtf8().constData()); }
  TaskInstanceList doCancelTaskInstancesByTaskId(QByteArray taskId);
  TaskInstanceList doAbortTaskInstanceByTaskId(QByteArray taskId);
  TaskInstance doAbortTaskInstance(quint64 id);
  void propagateTaskInstanceChange(TaskInstance instance);
  QMap<quint64,TaskInstance> detachedUnfinishedTaskInstances();
  void doShutdown(QDeadlineTimer deadline);
  void triggerPlanActions(TaskInstance instance);
  void triggerStartActions(TaskInstance instance);
  void triggerFinishActions(
      TaskInstance instance, std::function<bool(Action)> filter);
  void cancelOrAbortHerdedTasks(TaskInstance herder, bool allowEvenAbort);
  void planOrRequestCommonPostProcess(
    TaskInstance instance, TaskInstance herder,
    ParamSet overridingParams);
  QSet<TaskInstance> herdedTasks(quint64 herdid, bool includeFinished = true);
  void doTaskInstanceParamAppend(quint64 taskinstanceid, QString key,
                                 QString value);
};
#endif // SCHEDULER_H
