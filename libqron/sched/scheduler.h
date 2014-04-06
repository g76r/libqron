/* Copyright 2012-2014 Hallowyn and others.
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
#include "config/schedulerconfig.h"

class QThread;
class CronTrigger;

/** Core qron scheduler class.
 * Mainly responsible for configuration, queueing and event handling. */
class LIBQRONSHARED_EXPORT Scheduler : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(Scheduler)
  QThread *_thread;
  SchedulerConfig _config;
  QList<TaskInstance> _queuedRequests;
  QHash<TaskInstance,Executor*> _runningTasks;
  QList<Executor*> _availableExecutors;
  Alerter *_alerter;
  InMemoryAuthenticator *_authenticator;
  InMemoryUsersDatabase *_usersDatabase;
  bool _firstConfigurationLoad;
  qint64 _startdate, _configdate;
  long _execCount;
  QFileSystemWatcher *_accessControlFilesWatcher;
  PfNode _accessControlNode;

public:
  Scheduler();
  ~Scheduler();
  /** This method is thread-safe */
  bool loadConfig(QIODevice *source);
  void customEvent(QEvent *event);
  Alerter *alerter() { return _alerter; }
  Authenticator *authenticator() { return _authenticator; }
  UsersDatabase *usersDatabase() { return _usersDatabase; }
  QDateTime startdate() const {
    return QDateTime::fromMSecsSinceEpoch(_startdate); }
  QDateTime configdate() const {
    return _configdate == LLONG_MIN
        ? QDateTime() : QDateTime::fromMSecsSinceEpoch(_configdate); }
  long execCount() const { return _execCount; }
  int tasksCount() const { return _config.tasks().count(); }
  int tasksGroupsCount() const { return _config.tasksGroups().count(); }
  int maxtotaltaskinstances() const { return _config.maxtotaltaskinstances(); }
  int maxqueuedrequests() const { return _config.maxqueuedrequests(); }
  Calendar calendarByName(QString name) const;
  QHash<QString,Calendar> namedCalendars() const {
    return _config.namedCalendars(); }
  ParamSet globalParams() const { return _config.globalParams(); }
  /** This method is threadsafe */
  bool taskExists(QString fqtn);
  /** This method is threadsafe */
  Task task(QString fqtn);
  /** This method is threadsafe */
  void activateWorkflowTransition(TaskInstance workflowTaskInstance,
                                  QString transitionId, ParamSet eventContext);

public slots:
  /** Explicitely request task execution now.
   * This method will block current thread until the request is either
   * queued either denied by Scheduler thread.
   * If current thread is the Scheduler thread, the method is a direct call.
   * @param fqtn fully qualified task name, on the form "taskGroupId.taskId"
   * @param paramsOverriding override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @return isEmpty() if task cannot be queued
   * @see asyncRequestTask
   * @see RequestFormField */
  QList<TaskInstance> syncRequestTask(
      QString fqtn, ParamSet paramsOverriding = ParamSet(), bool force = false,
      TaskInstance callerTask = TaskInstance());
  /** Explicitely request task execution now, but do not wait for validity
   * check of the request, therefore do not wait for Scheduler thread
   * processing the request.
   * If current thread is the Scheduler thread, the call is queued anyway.
   * @param fqtn fully qualified task name, on the form "taskGroupId.taskId"
   * @param paramsOverriding override params, using RequestFormField semantics
   * @param force if true, any constraints or ressources are ignored
   * @see syncRequestTask
   * @see RequestFormField */
  void asyncRequestTask(const QString fqtn, ParamSet params = ParamSet(),
                        bool force = false,
                        TaskInstance callerTask = TaskInstance());
  /** Cancel a queued request.
   * @return TaskRequest.isNull() iff error (e.g. request not found or no longer
   * queued */
  TaskInstance cancelRequest(quint64 id);
  TaskInstance cancelRequest(TaskInstance instance) {
    return cancelRequest(instance.id()); }
  /** Abort a running request.
   * For local tasks aborting means killing, for ssh tasks aborting means
   * killing ssh client hence most of time killing actual task, for http tasks
   * aborting means closing the socket.
   * Beware that, but for local tasks, aborting a task does not guarantees that
   * the application processing is actually ended whereas it frees resources
   * and tasks instance counters, hence enabling immediate reexecution of the
   * same task.
   * @return TaskRequest.isNull() iff error (e.g. request not found or no longer
   * running */
  TaskInstance abortTask(quint64 id);
  TaskInstance abortTask(TaskInstance instance) {
    return abortTask(instance.id()); }
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
  bool enableTask(QString fqtn, bool enable);
  /** Enable or disable all tasks at once.
    * This method is threadsafe */
  void enableAllTasks(bool enable);
  //LATER enableAllTasksWithinGroup

signals:
  void logConfigurationChanged(QList<LogFile> logfiles);
  void globalParamsChanged(ParamSet globalParams);
  void globalSetenvChanged(ParamSet globalSetenv);
  void globalUnsetenvChanged(ParamSet globalUnsetenv);
  void hostsResourcesAvailabilityChanged(QString host,
                                         QHash<QString,qint64> resources);
  void accessControlConfigurationChanged(bool enabled);
  /** Emitted when config (re)load is complete, after all other config reload
   * signals: globalXxxChanged(), accessControlConfigurationChanged(),
   * hostResourceAllocationChanged, etc. */
  void configChanged(SchedulerConfig);
  /** There is no guarantee that taskQueued() is emited, taskStarted() or
    * taskFinished() can be emited witout previous taskQueued(). */
  void taskQueued(TaskInstance instance);
  /** There is no guarantee that taskStarted() is emited, taskFinished() can
    * be emited witout previous taskStarted(). */
  void taskStarted(TaskInstance instance);
  void taskFinished(TaskInstance instance);
  /** Called whenever a task or taskinstance changes: queued, started, finished,
   * disabled, enabled... */
  void taskChanged(Task instance);
  void noticePosted(QString notice, ParamSet params);

private slots:
  void taskFinishing(TaskInstance instance, QPointer<Executor> executor);
  void periodicChecks();
  /** Fire expired triggers for a given task. */
  void checkTriggersForTask(QVariant fqtn);
  /** Fire expired triggers for all tasks. */
  void checkTriggersForAllTasks();
  void reloadAccessControlConfig();

private:
  /** Reevaluate queued requests and start any task that can be started.
    * @see reevaluateQueuedRequests() */
  void startQueuedTasks();
  /** Check if it is permitted for a task to run now, if yes start it.
   * If instance.force() is true, start a task despite any constraint or limit,
   * even create a new (temporary) executor thread if needed.
   * @return true if the task was started or canceled */
  bool startQueuedTask(TaskInstance instance);
  /** @return true iff the triggers fires a task request */
  bool checkTrigger(CronTrigger trigger, Task task, QString fqtn);
  Q_INVOKABLE bool loadConfig(PfNode root);
  void setTimerForCronTrigger(CronTrigger trigger, QDateTime previous
                              = QDateTime::currentDateTime());
  Q_INVOKABLE QList<TaskInstance> doRequestTask(
      QString fqtn, ParamSet paramsOverriding, bool force,
      TaskInstance callerTask);
  TaskInstance enqueueRequest(TaskInstance request, ParamSet paramsOverriding);
  Q_INVOKABLE TaskInstance doCancelRequest(quint64 id);
  Q_INVOKABLE TaskInstance doAbortTask(quint64 id);
  Q_INVOKABLE void doActivateWorkflowTransition(
      TaskInstance workflowTaskInstance, QString transitionId, ParamSet eventContext);
};

#endif // SCHEDULER_H