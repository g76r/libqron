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
#include "scheduler.h"
#include <QtDebug>
#include <QCoreApplication>
#include <QEvent>
#include "pf/pfparser.h"
#include "pf/pfdomhandler.h"
#include "config/host.h"
#include "config/cluster.h"
#include <QMetaObject>
#include "log/log.h"
#include "log/filelogger.h"
#include <QFile>
#include "action/requesttaskaction.h"
#include <QThread>
#include "config/configutils.h"
#include "config/requestformfield.h"
#include "trigger/crontrigger.h"
#include "trigger/noticetrigger.h"
#include "thread/blockingtimer.h"

#define REEVALUATE_QUEUED_INSTANCES_EVENT (QEvent::Type(QEvent::User+1))
#define REEVALUATE_PLANNED_INSTANCES_EVENT (QEvent::Type(QEvent::User+2))
#define PERIODIC_CHECKS_INTERVAL_MS 60000

static SharedUiItem nullItem;

static int staticInit() {
  qMetaTypeId<TaskInstance>();
  qMetaTypeId<TaskInstanceList>();
  qMetaTypeId<QList<TaskInstance>>(); // TODO remove, it is probably no longer used
  qRegisterMetaType<QHash<QString,qint64>>("QHash<QString,qint64>");
  qMetaTypeId<ParamSet>();
  return 0;
}
Q_CONSTRUCTOR_FUNCTION(staticInit)

Scheduler::Scheduler() : QronConfigDocumentManager(0), _thread(new QThread()),
  _alerter(new Alerter), _authenticator(new InMemoryAuthenticator(this)),
  _usersDatabase(new InMemoryUsersDatabase(this)),
  _firstConfigurationLoad(true),
  _startdate(QDateTime::currentDateTime().toMSecsSinceEpoch()),
  _configdate(LLONG_MIN), _execCount(0), _runningTasksHwm(0),
  _queuedTasksHwm(0),
  _accessControlFilesWatcher(new QFileSystemWatcher(this)),
  _uniformRandomNumberGenerator(_randomDevice()) {
  _thread->setObjectName("SchedulerThread");
  connect(this, &Scheduler::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QThread::deleteLater);
  _thread->start();
  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &Scheduler::periodicChecks);
  timer->start(PERIODIC_CHECKS_INTERVAL_MS);
  connect(_accessControlFilesWatcher, &QFileSystemWatcher::fileChanged,
          this, &Scheduler::reloadAccessControlConfig);
  moveToThread(_thread);
}

Scheduler::~Scheduler() {
  //Log::removeLoggers();
  _alerter->deleteLater(); // TODO delete alerter only when last executor is deleted
}

void Scheduler::activateConfig(SchedulerConfig newConfig) {
  SchedulerConfig oldConfig = QronConfigDocumentManager::config();
  emit logConfigurationChanged(newConfig.logfiles());
  int executorsToAdd = newConfig.maxtotaltaskinstances()
      - oldConfig.maxtotaltaskinstances();
  if (executorsToAdd < 0) {
    if (-executorsToAdd > _availableExecutors.size()) {
      Log::warning() << "cannot set maxtotaltaskinstances down to "
                     << newConfig.maxtotaltaskinstances()
                     << " because there are too "
                        "currently many busy executors, setting it to "
                     << newConfig.maxtotaltaskinstances()
                        - (executorsToAdd - _availableExecutors.size())
                     << " instead";
      // TODO mark some executors as temporary to make them disappear later
      //maxtotaltaskinstances -= executorsToAdd - _availableExecutors.size();
      executorsToAdd = -_availableExecutors.size();
    }
    Log::debug() << "removing " << -executorsToAdd << " executors to reach "
                    "maxtotaltaskinstances of "
                 << newConfig.maxtotaltaskinstances();
    for (int i = 0; i < -executorsToAdd; ++i)
      _availableExecutors.takeFirst()->deleteLater();
  } else if (executorsToAdd > 0) {
    Log::debug() << "adding " << executorsToAdd << " executors to reach "
                    "maxtotaltaskinstances of "
                 << newConfig.maxtotaltaskinstances();
    for (int i = 0; i < executorsToAdd; ++i) {
      Executor *executor = new Executor(_alerter);
      connect(executor, &Executor::taskInstanceStopped,
              this, [this](TaskInstance instance, Executor *executor) {
        taskInstanceStoppedOrCanceled(instance, executor, false);
      });
      connect(executor, &Executor::taskInstanceStarted,
              this, &Scheduler::propagateTaskInstanceChange);
      connect(this, &Scheduler::noticePosted,
              executor, &Executor::noticePosted);
      connect(this, &QObject::destroyed, executor, &QObject::deleteLater);
      _availableExecutors.append(executor);
    }
  } else {
    Log::debug() << "keep maxtotaltaskinstances of "
                 << newConfig.maxtotaltaskinstances();
  }
  newConfig.copyLiveAttributesFromOldTasks(oldConfig.tasks());
  QMutexLocker ml(&_configGuard);
  setConfig(newConfig, &ml);
  _alerter->setConfig(newConfig.alerterConfig());
  reloadAccessControlConfig();
  QMetaObject::invokeMethod(this, [this](){
    checkTriggersForAllTasks();
  }, Qt::QueuedConnection);
  foreach (const QString &host, _consumedResources.keys()) {
    const QHash<QString,qint64> &hostConsumedResources
        = _consumedResources[host];
    QHash<QString,qint64> hostAvailableResources
        = newConfig.hosts().value(host).resources();
    foreach (const QString &kind, hostConsumedResources.keys())
      hostAvailableResources.insert(kind, hostAvailableResources.value(kind)
                                    - hostConsumedResources.value(kind));
    emit hostsResourcesAvailabilityChanged(host, hostAvailableResources);
  }
  reevaluateQueuedTaskInstances();
  // inspect queued requests to replace Task objects or remove request
  auto queued = _queuedTasks;
  queued.detach();
  for (auto instance: queued) {
    QString taskId = instance.task().id();
    Task t = newConfig.tasks().value(taskId);
    if (t.isNull()) {
      doCancelTaskInstance(instance, true,
                      "canceling queued task while reloading configuration "
                      "because this task no longer exists");
    } else {
      Log::info(taskId, instance.id())
          << "replacing task definition in queued request while reloading "
             "configuration";
      instance.setTask(t);
    }
  }
  _configdate = QDateTime::currentDateTime().toMSecsSinceEpoch();
  if (_firstConfigurationLoad) {
    _firstConfigurationLoad = false;
    Log::info() << "starting scheduler";
    foreach(EventSubscription sub, newConfig.onschedulerstart())
      sub.triggerActions();
  }
  foreach(EventSubscription sub, newConfig.onconfigload())
    sub.triggerActions();
}

void Scheduler::reloadAccessControlConfig() {
  config().accessControlConfig().applyConfiguration(
        _authenticator, _usersDatabase, _accessControlFilesWatcher);
}

static bool planOrRequestCommonPreProcess(
    QString taskId, Task task, ParamSet overridingParams) {
  if (task.isNull()) {
    Log::error() << "requested task not found: " << taskId;
    return false;
  }
  if (!task.enabled()) {
    Log::info(taskId) << "ignoring request since task is disabled: " << taskId;
    return false;
  }
  bool fieldsValidated = true;
  for (auto field: task.requestFormFields()) {
    QString name = field.id();
    if (!overridingParams.contains(name))
      continue;
    QString value = overridingParams.value(name);
    if (!field.validate(value)) {
      Log::error() << "task " << taskId
                   << " requested with an invalid parameter override: '"
                   << name << "'' set to '"
                   << value << "' whereas format is '" << field.format()
                   << "'";
      fieldsValidated = false;
    }
  }
  if (!fieldsValidated)
    return false;
  return true;
}

static void planOrRequestCommonPostProcess(
    TaskInstance instance, TaskInstance herder,
    QHash<TaskInstance,TaskInstanceList> &waitingTasks,
    ParamSet overridingParams) {
  auto params = instance.params();
  auto instanceparams = instance.task().instanceparams();
  auto ppp = instance.pseudoParams();
  for (auto key: instanceparams.keys()) {
    auto value = params.value(key, &ppp);
    instance.setParam(key, ParamSet::escape(value));
  }
  for (auto key: overridingParams.keys()) {
    auto value = params.value(key, &ppp);
    instance.setParam(key, ParamSet::escape(value));
  }
  if (herder.isNull() || herder == instance)
    return;
  if (waitingTasks.contains(herder))
    waitingTasks[herder].append(instance);
  Log::info(herder.task().id(), herder.id())
      << "task appended to herded tasks: "
      << instance.task().id()+"/"+instance.id();
}

TaskInstanceList Scheduler::requestTask(
    QString taskId, ParamSet overridingParams, bool force, QString herdId) {
  if (this->thread() == QThread::currentThread())
    return doRequestTask(taskId, overridingParams, force, herdId);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
                            force,herdId](){
      instances = doRequestTask(taskId, overridingParams, force, herdId);
    }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::requestTask(
    QString taskId, ParamSet overridingParams, bool force, TaskInstance herder) {
  if (this->thread() == QThread::currentThread())
    return doRequestTask(taskId, overridingParams, force, herder);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
                            force,herder](){
      instances = doRequestTask(taskId, overridingParams, force, herder);
    }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::doRequestTask(
    QString taskId, ParamSet overridingParams, bool force, QString herdId) {
  TaskInstance herder = _unfinishedTasks.value(herdId.toLongLong());
  return doRequestTask(taskId, overridingParams, force, herder);
}

TaskInstanceList Scheduler::doRequestTask(
    QString taskId, ParamSet overridingParams, bool force,
    TaskInstance herder) {
  TaskInstanceList instances;
  Task task = config().tasks().value(taskId);
  if (!planOrRequestCommonPreProcess(taskId, task, overridingParams))
    return instances;
  Cluster cluster = config().clusters().value(task.target());
  if (cluster.balancing() == Cluster::Each) {
    quint64 groupId = 0;
    foreach (Host host, cluster.hosts()) {
      TaskInstance instance(task, groupId, force,  overridingParams, herder);
      if (!groupId) { // first iteration
        groupId = instance.groupId();
        if (herder.isNull())
          herder = instance;
      }
      instance.setTarget(host);
      instance = enqueueTaskInstance(instance);
      if (!instance.isNull())
        instances.append(instance);
    }
  } else {
    TaskInstance request(task, force, overridingParams, herder);
    request = enqueueTaskInstance(request);
    if (!request.isNull())
      instances.append(request);
  }
  if (!instances.isEmpty()) {
    for (auto instance: instances) {
      planOrRequestCommonPostProcess(instance, herder, _waitingTasks,
                                     overridingParams);
      emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
      triggerPlanActions(instance);
      Log::debug(taskId, instance.idAsLong())
          << "task instance params after planning" << instance.params();
      if (!herder.isNull() && herder != instance) {
        emit itemChanged(herder, herder, QStringLiteral("taskinstance"));
        reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
      }
    }
  }
  return instances;
}

TaskInstanceList Scheduler::planTask(
    QString taskId, ParamSet overridingParams, bool force, QString herdId,
    Condition queuewhen, Condition cancelwhen) {
  if (this->thread() == QThread::currentThread())
    return doPlanTask(taskId, overridingParams, force, herdId, queuewhen,
                      cancelwhen);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
             force,herdId,queuewhen,cancelwhen](){
        instances = doPlanTask(taskId, overridingParams, force, herdId,
                               queuewhen, cancelwhen);
      }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::planTask(
    QString taskId, ParamSet overridingParams, bool force, TaskInstance herder,
    Condition queuewhen, Condition cancelwhen) {
  if (this->thread() == QThread::currentThread())
    return doPlanTask(taskId, overridingParams, force, herder, queuewhen,
                      cancelwhen);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
             force,herder,queuewhen,cancelwhen](){
        instances = doPlanTask(taskId, overridingParams, force, herder,
                               queuewhen, cancelwhen);
      }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::doPlanTask(
    QString taskId, ParamSet overridingParams, bool force, QString herdId,
    Condition queuewhen, Condition cancelwhen) {
  TaskInstance herder = _unfinishedTasks.value(herdId.toLongLong());
  return doPlanTask(taskId, overridingParams, force, herder, queuewhen,
                    cancelwhen);
}

TaskInstanceList Scheduler::doPlanTask(
    QString taskId, ParamSet overridingParams, bool force, TaskInstance herder,
    Condition queuewhen, Condition cancelwhen) {
  TaskInstanceList instances;
  Task task = config().tasks().value(taskId);
  if (!planOrRequestCommonPreProcess(taskId, task, overridingParams))
    return instances;
  Cluster cluster = config().clusters().value(task.target());
  if (cluster.balancing() == Cluster::Each) {
    Log::error() << "cannot plan task when its target is a each balancing "
                    "cluster: " << taskId;
    return instances;
  }
  if (herder.isNull()) {
    Log::error() << "wont plan a task that is not herded" << taskId;
    return instances;
  }
  TaskInstance instance(task, force, overridingParams, herder, queuewhen,
                        cancelwhen);
  _unfinishedTasks.insert(instance.idAsLong(), instance);
  herder.appendHerdedTask(instance);
  Log::debug(taskId, instance.idAsLong())
      << "planning task " << taskId << "/" << instance.id() << " "
      << overridingParams << " with herdid " << instance.herdid()
      << " and queue condition " << instance.queuewhen().toString()
      << " and cancel condition " << instance.cancelwhen().toString();
  planOrRequestCommonPostProcess(instance, herder, _waitingTasks,
                                 overridingParams);
  emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
  triggerPlanActions(instance);
  emit itemChanged(herder, herder, QStringLiteral("taskinstance"));
  Log::debug(taskId, instance.idAsLong())
      << "task instance params after planning" << instance.params();
  if (!herder.isNull())
    reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
  instances.append(instance);
  return instances;
}

TaskInstance Scheduler::enqueueTaskInstance(TaskInstance instance) {
  Task task(instance.task());
  QString taskId(task.id());
  if (_shutingDown) {
    Log::warning(taskId, instance.idAsLong())
        << "cannot queue task because scheduler is shuting down";
    return TaskInstance();
  }
  if (!instance.force()) {
    if (task.enqueuePolicy() == Task::EnqueueUntilMaxInstances) {
      // reject task request if running count + queued count >= max instance
      int count = task.runningCount();
      int max = task.maxInstances();
      if (count < max) {
        for (const TaskInstance &request : _queuedTasks) {
          if (request.task().id() == taskId)
            ++count;
        }
      }
      if (count >= max) {
        QString msg = "canceling task because too many instances of this task "
                      "are already queued and enqueuepolicy is "
            + task.enqueuePolicyAsString();
        if (!task.enabled())
          msg += " and the task is disabled";
        msg += " : " + taskId + "/" + instance.id();
        doCancelTaskInstance(instance, !task.enabled(), msg);
        instance.herder().appendHerdedTask(instance);
        return instance;
      }
    }
    if (!task.enabled()
        || task.enqueuePolicy() == Task::EnqueueAndDiscardQueued) {
      // avoid stacking disabled task requests by canceling older ones
      auto queued = _queuedTasks;
      queued.detach();
      for (auto other: queued) {
        if (taskId == other.task().id()
            && instance.groupId() != other.groupId()) {
          QString msg = "canceling task because another instance of the same "
                        "task is queued";
          if (!task.enabled())
            msg += " and the task is disabled";
          if (task.enqueuePolicy() == Task::EnqueueAndDiscardQueued)
            msg += " and enqueuepolicy is " + task.enqueuePolicyAsString();
          msg += " : " + taskId + "/" + instance.id();
          doCancelTaskInstance(other, !task.enabled(), msg);
        }
      }
    }
  }
  if (_queuedTasks.size() >= config().maxqueuedrequests()) {
    Log::error(taskId, instance.idAsLong())
        << "cannot queue task because maxqueuedrequests is already reached ("
        << config().maxqueuedrequests() << ")";
    _alerter->raiseAlert("scheduler.maxqueuedrequests.reached");
    return TaskInstance();
  }
  instance.setQueueDatetime();
  _unfinishedTasks.insert(instance.idAsLong(), instance);
  instance.herder().appendHerdedTask(instance);
  _alerter->cancelAlert("scheduler.maxqueuedrequests.reached");
  Log::debug(taskId, instance.idAsLong())
      << "queuing task " << taskId << "/" << instance.id()
      << " with request group id " << instance.groupId()
      << " and herdid " << instance.herdid();
  // note: a request must always be queued even if the task can be started
  // immediately, to avoid the new tasks being started before queued ones
  _queuedTasks.append(instance);
  if (_queuedTasks.size() > _queuedTasksHwm)
    _queuedTasksHwm = _queuedTasks.size();
  reevaluateQueuedTaskInstances();
  return instance;
}

TaskInstance Scheduler::cancelTaskInstance(quint64 id) {
  if (this->thread() == QThread::currentThread())
    return doCancelTaskInstance(_unfinishedTasks.value(id), false,
                        "canceling task as requested");
  TaskInstance instance;
  QMetaObject::invokeMethod(this, [this,&instance,id](){
    instance = doCancelTaskInstance(_unfinishedTasks.value(id), false,
                            "canceling task as requested");
    }, Qt::BlockingQueuedConnection);
  return instance;
}

TaskInstance Scheduler::doCancelTaskInstance(
    TaskInstance instance, bool warning, const char *reason) {
  switch (instance.status()) {
  case TaskInstance::Planned:
  case TaskInstance::Queued: {
      if (warning)
        Log::warning(instance.task().id(), instance.id()) << reason;
      else
        Log::info(instance.task().id(), instance.id()) << reason;
      taskInstanceStoppedOrCanceled(instance, 0, false);
      return instance;
    }
  case TaskInstance::Running:
  case TaskInstance::Waiting:
  case TaskInstance::Success:
  case TaskInstance::Failure:
  case TaskInstance::Canceled:
    break;
  }
  Log::warning(instance.task().id(), instance.id())
      << "cannot cancel task instance because it was found in a cancelable "
         "status, was in status: " << instance.statusAsString();
  return TaskInstance();

}

TaskInstanceList Scheduler::cancelTaskInstancesByTaskId(QString taskId) {
  if (this->thread() == QThread::currentThread())
    return doCancelTaskInstancesByTaskId(taskId);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId](){
    instances = doCancelTaskInstancesByTaskId(taskId);
    }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::doCancelTaskInstancesByTaskId(QString taskId) {
  TaskInstanceList instances;
  for (int i = 0; i < _queuedTasks.size(); ++i) {
    TaskInstance r = _queuedTasks[i];
    if (taskId == r.task().id())
      instances.append(r);
  }
  for (const TaskInstance &instance : instances)
    if (doCancelTaskInstance(instance, false, "canceling task as requested")
            .isNull())
      instances.removeOne(instance);
  return instances;
}

TaskInstance Scheduler::abortTaskInstance(quint64 id) {
  if (this->thread() == QThread::currentThread())
    return doAbortTaskInstance(id);
  TaskInstance taskInstance;
  QMetaObject::invokeMethod(this, [this,&taskInstance,id](){
    taskInstance = doAbortTaskInstance(id);
    }, Qt::BlockingQueuedConnection);
  return taskInstance;
}

TaskInstance Scheduler::doAbortTaskInstance(quint64 id) {
  for (auto instance: _runningTasks.keys()) {
    if (id == instance.idAsLong()) {
      QString taskId(instance.task().id());
      Executor *executor = _runningTasks.value(instance);
      if (executor) {
        Log::warning(taskId, id) << "aborting running task as requested";
        // TODO should return TaskInstance() if executor cannot actually abort
        executor->abort();
        return instance;
      }
    }
  }
  TaskInstance instance = _unfinishedTasks.value(id);
  QString taskId = instance.task().id();
  if (!instance.isNull() && _waitingTasks.contains(instance)) {
    // LATER abort or cancel every sheep, depending on its status
  }
  Log::warning() << "cannot abort task because it is not in running tasks list";
  return TaskInstance();
}

TaskInstanceList Scheduler::abortTaskInstanceByTaskId(QString taskId) {
  if (this->thread() == QThread::currentThread())
    return doAbortTaskInstanceByTaskId(taskId);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId](){
    instances = doAbortTaskInstanceByTaskId(taskId);
    }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::doAbortTaskInstanceByTaskId(QString taskId) {
  TaskInstanceList instances;
  TaskInstanceList tasks = _runningTasks.keys();
  for (int i = 0; i < tasks.size(); ++i) {
    TaskInstance r = tasks[i];
    if (taskId == r.task().id())
      instances.append(r);
  }
  for (const TaskInstance &r : instances)
    if (doAbortTaskInstance(r.idAsLong()).isNull())
      instances.removeOne(r);
  return instances;
}

void Scheduler::checkTriggersForTask(QVariant taskId) {
  //Log::debug() << "Scheduler::checkTriggersForTask " << taskId;
  Task task = config().tasks().value(taskId.toString());
  foreach (const CronTrigger trigger, task.cronTriggers())
    checkTrigger(trigger, task, taskId.toString());
}

void Scheduler::checkTriggersForAllTasks() {
  //Log::debug() << "Scheduler::checkTriggersForAllTasks ";
  QList<Task> tasksWithoutTimeTrigger;
  foreach (Task task, config().tasks().values()) {
    QString taskId = task.id();
    foreach (const CronTrigger trigger, task.cronTriggers())
      checkTrigger(trigger, task, taskId);
    if (task.cronTriggers().isEmpty()) {
      task.setNextScheduledExecution(QDateTime());
      tasksWithoutTimeTrigger.append(task);
    }
  }
  // LATER if this is usefull only to remove next exec time when reloading config w/o time trigger, this should be called in reloadConfig
  foreach (const Task task, tasksWithoutTimeTrigger)
    emit itemChanged(task, task, QStringLiteral("task"));
}

bool Scheduler::checkTrigger(CronTrigger trigger, Task task, QString taskId) {
  //Log::debug() << "Scheduler::checkTrigger " << trigger.cronExpression()
  //             << " " << taskId;
  QDateTime now(QDateTime::currentDateTime());
  QDateTime next = trigger.nextTriggering();
  bool fired = false;
  if (next <= now) {
    // requestTask if trigger reached
    ParamSet overridingParams;
    // FIXME globalParams context evaluation is probably buggy, see what is done
    // for notices and fix this
    foreach (QString key, trigger.overridingParams().keys())
      overridingParams
          .setValue(key, config().params()
                    .value(trigger.overridingParams().rawValue(key)));
    TaskInstanceList requests = requestTask(taskId, overridingParams);
    if (!requests.isEmpty())
      for (TaskInstance request : requests)
        Log::info(taskId, request.idAsLong())
            << "cron trigger " << trigger.humanReadableExpression()
            << " triggered task " << taskId;
    //else
    //Log::debug(taskId) << "cron trigger " << trigger.humanReadableExpression()
    //                   << " failed to trigger task " << taskId;
    trigger.setLastTriggered(now);
    next = trigger.nextTriggering();
    fired = true;
  } else {
    QDateTime taskNext = task.nextScheduledExecution();
    if (taskNext.isValid() && taskNext <= next && taskNext > now) {
      //Log::debug() << "Scheduler::checkTrigger don't trigger or plan new "
      //                "check for task " << taskId << " "
      //             << now.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
      //             << next.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
      //             << taskNext.toString("yyyy-MM-dd hh:mm:ss,zzz");
      return false; // don't plan new check if already planned
    }
  }
  if (next.isValid()) {
    // plan new check
    qint64 ms = now.msecsTo(next);
    //Log::debug() << "Scheduler::checkTrigger planning new check for task "
    //             << taskId << " "
    //             << now.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
    //             << next.toString("yyyy-MM-dd hh:mm:ss,zzz") << " " << ms;
    // LATER one timer per trigger, not a new timer each time
    QTimer::singleShot(ms < INT_MAX ? ((int)ms) : INT_MAX, Qt::PreciseTimer,
                       this, [this,taskId](){
      checkTriggersForTask(taskId);
    });
    task.setNextScheduledExecution(now.addMSecs(ms));
  } else {
    task.setNextScheduledExecution(QDateTime());
  }
  emit itemChanged(task, task, QStringLiteral("task"));
  return fired;
}

void Scheduler::postNotice(QString notice, ParamSet params) {
  if (notice.isNull()) {
    Log::warning() << "cannot post a null/empty notice";
    return;
  }
  QHash<QString,Task> tasks = config().tasks();
  if (params.parent().isNull())
    params.setParent(config().params());
  params.setValue(QStringLiteral("!notice"), notice);
  Log::debug() << "posting notice ^" << notice << " with params " << params;
  foreach (Task task, tasks.values()) {
    foreach (NoticeTrigger trigger, task.noticeTriggers()) {
      // LATER implement regexp patterns for notice triggers
      if (trigger.expression() == notice) {
        Log::info() << "notice " << trigger.humanReadableExpression()
                    << " triggered task " << task.id();
        // FIXME check calendar
        ParamSet overridingParams;
        foreach (QString key, trigger.overridingParams().keys()) {
          overridingParams.setValue(
                key,
                ParamSet::escape(
                  trigger.overridingParams().value(key, &params)));
        }
        TaskInstanceList requests
            = requestTask(task.id(), overridingParams);
        if (!requests.isEmpty())
          foreach (TaskInstance request, requests)
            Log::info(task.id(), request.idAsLong())
                << "notice " << trigger.humanReadableExpression()
                << " triggered task " << task.id();
        else
          Log::debug(task.id())
              << "notice " << trigger.humanReadableExpression()
              << " failed to trigger task " << task.id();
      }
    }
  }
  emit noticePosted(notice, params);
  // TODO filter onnotice events
  foreach (EventSubscription sub, config().onnotice())
    sub.triggerActions(params);
}

void Scheduler::reevaluateQueuedTaskInstances() {
  QCoreApplication::postEvent(
      this, new QEvent(REEVALUATE_QUEUED_INSTANCES_EVENT));
}

void Scheduler::reevaluatePlannedTaskInstancesForHerd(quint64 herdid) {
  _dirtyHerds.insert(herdid);
  QCoreApplication::postEvent(
      this, new QEvent(REEVALUATE_PLANNED_INSTANCES_EVENT));
}

void Scheduler::customEvent(QEvent *event) {
  auto t = event->type();
  if (t == REEVALUATE_QUEUED_INSTANCES_EVENT) {
    QCoreApplication::removePostedEvents(
        this, REEVALUATE_QUEUED_INSTANCES_EVENT);
    startAsManyTaskInstancesAsPossible();
    return;
  }
  if (t == REEVALUATE_PLANNED_INSTANCES_EVENT) {
    QCoreApplication::removePostedEvents(
        this, REEVALUATE_PLANNED_INSTANCES_EVENT);
    enqueueAsManyTaskInstancesAsPossible();
    return;
  }
  QObject::customEvent(event);
}

void Scheduler::enqueueAsManyTaskInstancesAsPossible() {
  if (_shutingDown)
    return;
  auto dirtyHerds = _dirtyHerds;
  _dirtyHerds.clear();
  for (auto instance: _unfinishedTasks) {
    if (!dirtyHerds.contains(instance.herdid()))
      continue;
    if (instance.status() != TaskInstance::Planned)
      continue;
    Condition queuewhen = instance.queuewhen();
    if (queuewhen.evaluate(instance)) {
      Log::info(instance.task().id(), instance.id())
          << "queuing task because queue condition is met: "
          << queuewhen.toString();
      instance = enqueueTaskInstance(instance);
      itemChanged(instance, instance, QStringLiteral("taskinstance"));
      continue;
    }
    Condition cancelwhen = instance.cancelwhen();
    if (cancelwhen.evaluate(instance)) {
      doCancelTaskInstance(instance, false,
                           "canceling task because cancel condition is met: "
                               +cancelwhen.toString());
      continue;
    }
  }
}

void Scheduler::startAsManyTaskInstancesAsPossible() {
  if (_shutingDown)
    return;
  auto queued = _queuedTasks;
  queued.detach();
  for (auto instance: queued) {
    if (instance.status() != TaskInstance::Queued)
      continue; // changed meanwhile, e.g. canceled
    auto task = instance.task();
    if (startTaskInstance(instance)) {
      // discarding queued taskinstances when starting in addition to when
      // queuing is most of the time useless, however it make it possible to
      // handle some misconfigurations like (onstart(requesttask %!taskid)) or
      // more generaly it discard requests received between the current one
      // and the actual task start
      if (task.enqueuePolicy() == Task::EnqueueAndDiscardQueued) {
        // remove other requests of same task
        QString taskId= instance.task().id();
        auto queued = _queuedTasks;
        queued.detach();
        for (auto other: queued) {
          if (task.id() == other.task().id()
              && instance.groupId() != other.groupId()) {
            doCancelTaskInstance(other, false,
                         "canceling task because another instance of the same "
                         "task is starting, other task is: "
                                 +taskId+"/"+instance.id());
          }
        }
        emit itemChanged(task, task, QStringLiteral("task"));
      }
    }
  }
}

bool Scheduler::startTaskInstance(TaskInstance instance) {
  Task task(instance.task());
  QString taskId = task.id();
  Executor *executor = 0;
  if (!task.enabled())
    return false; // do not start disabled tasks
  if (_availableExecutors.isEmpty() && !instance.force()) {
    QString s;
    QDateTime now = QDateTime::currentDateTime();
    foreach (const TaskInstance &ti, _runningTasks.keys())
      s.append(ti.id()).append(' ')
          .append(ti.task().id()).append(" since ")
          .append(QString::number(ti.startDatetime().msecsTo(now)))
          .append(" ms; ");
    if (s.size() >= 2)
      s.chop(2);
    Log::info(taskId, instance.idAsLong()) << "cannot execute task '" << taskId
        << "' now because there are already too many tasks running "
           "(maxtotaltaskinstances reached) currently running tasks: " << s;
    _alerter->raiseAlert("scheduler.maxtotaltaskinstances.reached");
    return false;
  }
  _alerter->cancelAlert("scheduler.maxtotaltaskinstances.reached");
  if (instance.force())
    task.fetchAndAddRunningCount(1);
  else if (task.fetchAndAddRunningCount(1) >= task.maxInstances()) {
    task.fetchAndAddRunningCount(-1);
    Log::warning() << "requested task '" << taskId << "' cannot be executed "
                      "because maxinstances is already reached ("
                   << task.maxInstances() << ")";
    _alerter->raiseAlert("task.maxinstancesreached."+taskId);
    return false;
  }
  _alerter->cancelAlert("task.maxinstancesreached."+taskId);
  QString target = instance.target().id();
  if (target.isEmpty()) // use task target if not overiden at intance level
    target = task.target();
  SharedUiItemList<Host> hosts;
  Host host = config().hosts().value(target);
  if (host.isNull()) {
    Cluster cluster = config().clusters().value(target);
    hosts = cluster.hosts();
    switch (cluster.balancing()) {
    case Cluster::First:
    case Cluster::Each:
    case Cluster::UnknownBalancing:
      // nothing to do
      break;
    case Cluster::Random:
      std::shuffle(hosts.begin(), hosts.end(), _uniformRandomNumberGenerator);
      break;
    case Cluster::RoundRobin:
      // perform circular permutation of hosts depending on tasks exec count
      if (!hosts.isEmpty())
        for (int shift = task.executionsCount() % hosts.size(); shift; --shift)
          hosts.append(hosts.takeFirst());
      break;
    }
    //qDebug() << "*** balancing:" << cluster.balancingAsString() << "hosts:"
    //         << hosts.join(' ') << "exec:" << task.executionsCount();
  } else {
    hosts.append(host);
  }
  if (hosts.isEmpty()) {
    Log::error(taskId, instance.idAsLong()) << "cannot execute task '" << taskId
        << "' because its target '" << target << "' is invalid";
    task.fetchAndAddRunningCount(-1);
    taskInstanceStoppedOrCanceled(instance, 0, true);
    return true;
  }
  // LATER implement best effort resource check for forced requests
  QHash<QString,qint64> taskResources = task.resources();
  foreach (Host h, hosts) {
    // LATER skip hosts currently down or disabled
    if (!taskResources.isEmpty()) {
      QHash<QString,qint64> hostConsumedResources
          = _consumedResources.value(h.id());
      QHash<QString,qint64> hostAvailableResources
          = config().hosts().value(h.id()).resources();
      if (!instance.force()) {
        foreach (QString kind, taskResources.keys()) {
          qint64 alreadyConsumed = hostConsumedResources.value(kind);
          qint64 stillAvailable = hostAvailableResources.value(kind)-alreadyConsumed;
          qint64 needed = taskResources.value(kind);
          if (stillAvailable < needed ) {
            Log::info(taskId, instance.idAsLong())
                << "lacks resource '" << kind << "' on host '" << h.id()
                << "' for task '" << task.id() << "' (need " << needed
                << ", have " << stillAvailable << ")";
            goto nexthost;
          }
          hostConsumedResources.insert(kind, alreadyConsumed+needed);
          hostAvailableResources.insert(kind, stillAvailable-needed);
          Log::debug(taskId, instance.idAsLong())
              << "resource '" << kind << "' ok on host '" << h.id()
              << "' for task '" << taskId << "'";
        }
      }
      // a host with enough resources was found
      _consumedResources.insert(h.id(), hostConsumedResources);
      emit hostsResourcesAvailabilityChanged(h.id(), hostAvailableResources);
    }
    _alerter->cancelAlert("task.resource_exhausted."+taskId);
    instance.setTarget(h);
    instance.setStartDatetime();
    executor = _availableExecutors.takeFirst();
    if (!executor) {
      // this should only happen with force == true
      executor = new Executor(_alerter);
      executor->setTemporary();
      connect(executor, &Executor::taskInstanceStopped,
              this, [this](TaskInstance instance, Executor *executor) {
        taskInstanceStoppedOrCanceled(instance, executor, false);
      });
      connect(executor, &Executor::taskInstanceStarted,
              this, &Scheduler::propagateTaskInstanceChange);
      connect(this, &Scheduler::noticePosted,
              executor, &Executor::noticePosted);
      connect(this, &QObject::destroyed, executor, &QObject::deleteLater);
    }
    executor->execute(instance);
    task.fetchAndAddExecutionsCount(1);
    ++_execCount;
    _queuedTasks.removeOne(instance);
    _runningTasks.insert(instance, executor);
    if (_runningTasks.size() > _runningTasksHwm)
      _runningTasksHwm = _runningTasks.size();
    triggerStartActions(instance);
    reevaluateQueuedTaskInstances();
    return true;
nexthost:;
  }
  // no host has enough resources to execute the task
  task.fetchAndAddRunningCount(-1);
  Log::warning(taskId, instance.idAsLong())
      << "cannot execute task '" << taskId
      << "' now because there is not enough resources on target '"
      << target << "'";
  // LATER suffix alert with resources kind (one alert per exhausted kind)
  _alerter->raiseAlert("task.resource_exhausted."+taskId);
  return false;
}

void Scheduler::taskInstanceStoppedOrCanceled(
    TaskInstance instance, Executor *executor, bool processCanceledAsFailure) {
  if (executor) {
    // deleteLater() because it lives in its own thread
    if (executor->isTemporary())
      executor->deleteLater();
    else
      _availableExecutors.append(executor);
  }
  _runningTasks.remove(instance);
  auto herder = instance.herder();
  if (herder != instance) {
    taskInstanceFinishedOrCanceled(instance, processCanceledAsFailure);
    if (_waitingTasks.contains(herder)) {
      TaskInstanceList &sheeps = _waitingTasks[herder];
      sheeps.removeOne(instance);
      Log::info(herder.task().id(), herder.id())
          << "herded task finished: " << instance.task().id()+"/"+instance.id()
          << " remaining: " << sheeps.size() << " : " << sheeps;
      if (sheeps.isEmpty())
        taskInstanceFinishedOrCanceled(herder, false);
      else
        reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
    }
    return;
  }
  triggerFinishActions(instance, [](Action a) {
    return a.mayCreateTaskInstances();
  });
  TaskInstanceList livingSheeps;
  for (auto sheep: instance.herdedTasks()) {
    if (!sheep.isFinished())
      livingSheeps.append(sheep);
  }
  // configured and requested tasks are different if config was reloaded
  Task configuredTask = config().tasks().value(instance.task().id());
  if (instance.status() == TaskInstance::Canceled || livingSheeps.isEmpty()
      || configuredTask.herdingPolicy() == Task::NoWait) {
    taskInstanceFinishedOrCanceled(instance, processCanceledAsFailure);
    return;
  }
  Log::info(instance.task().id(), instance.id())
      << "waiting for herded tasks to finish: " << livingSheeps;
  _waitingTasks.insert(instance, livingSheeps);
  emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
}

void Scheduler::taskInstanceFinishedOrCanceled(
    TaskInstance instance, bool processCanceledAsFailure) {
  Task requestedTask = instance.task();
  QString taskId = requestedTask.id();
  // configured and requested tasks are different if config was reloaded
  Task configuredTask = config().tasks().value(taskId);
  auto now = QDateTime::currentDateTime();
  instance.setFinishDatetime(now);
  if (instance.status() == TaskInstance::Canceled) { // i.e. start date not set
    instance.setReturnCode(-1);
    instance.setSuccess(false);
    if (processCanceledAsFailure)
      instance.setStartDatetime(now);
    instance.setStopDatetime(now);
    if (instance.task().runningCount() < instance.task().maxInstances())
      _alerter->cancelAlert("task.maxinstancesreached."+taskId);
    _queuedTasks.removeOne(instance);
  } else {
    instance.setHerderSuccess(instance.task().herdingPolicy());
    configuredTask.fetchAndAddRunningCount(-1);
    QHash<QString,qint64> taskResources = requestedTask.resources();
    QHash<QString,qint64> hostConsumedResources =
        _consumedResources.value(instance.target().id());
    QHash<QString,qint64> hostAvailableResources =
        config().hosts().value(instance.target().id()).resources();
    foreach (QString kind, taskResources.keys()) {
      qint64 qty = hostConsumedResources.value(kind)-taskResources.value(kind);
      hostConsumedResources.insert(kind, qty);
      hostAvailableResources.insert(kind, hostAvailableResources.value(kind)-qty);
    }
    _consumedResources.insert(instance.target().id(), hostConsumedResources);
    emit hostsResourcesAvailabilityChanged(instance.target().id(),
                                           hostAvailableResources);
    if (instance.success())
      _alerter->cancelAlert("task.failure."+taskId);
    else
      _alerter->raiseAlert("task.failure."+taskId);
    configuredTask.setLastExecution(instance.startDatetime());
    configuredTask.setLastSuccessful(instance.success());
    configuredTask.setLastReturnCode(instance.returnCode());
    configuredTask.setLastTotalMillis((int)instance.totalMillis());
    configuredTask.setLastTaskInstanceId(instance.idAsLong());
    triggerFinishActions(instance, [instance](Action a) {
      return instance.idAsLong() != instance.herdid()
          || !a.mayCreateTaskInstances();
    });
    if (configuredTask.maxExpectedDuration() < LLONG_MAX) {
      if (configuredTask.maxExpectedDuration() < instance.totalMillis())
        _alerter->raiseAlert("task.toolong."+taskId);
      else
        _alerter->cancelAlert("task.toolong."+taskId);
    }
    if (configuredTask.minExpectedDuration() > 0) {
      if (configuredTask.minExpectedDuration() > instance.runningMillis())
        _alerter->raiseAlert("task.tooshort."+taskId);
      else
        _alerter->cancelAlert("task.tooshort."+taskId);
    }
    _waitingTasks.remove(instance);
  }
  _unfinishedTasks.remove(instance.idAsLong());
  emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
  emit itemChanged(configuredTask, configuredTask, QStringLiteral("task"));
  reevaluateQueuedTaskInstances();
}

bool Scheduler::enableTask(QString taskId, bool enable) {
  Task t = config().tasks().value(taskId);
  //Log::fatal() << "enableTask " << taskId << " " << enable << " " << t.id();
  if (t.isNull())
    return false;
  t.setEnabled(enable);
  if (enable)
    reevaluateQueuedTaskInstances();
  emit itemChanged(t, t, QStringLiteral("task"));
  return true;
}

void Scheduler::enableAllTasks(bool enable) {
  QList<Task> tasks(config().tasks().values());
  foreach (Task t, tasks) {
    t.setEnabled(enable);
    emit itemChanged(t, t, QStringLiteral("task"));
  }
  if (enable)
    reevaluateQueuedTaskInstances();
}

void Scheduler::periodicChecks() {
  // detect queued or running tasks that exceeded their max expected duration
  TaskInstanceList currentInstances;
  currentInstances.append(_queuedTasks);
  currentInstances.append(_runningTasks.keys());
  foreach (const TaskInstance r, currentInstances) {
    const Task t(r.task());
    if (t.maxExpectedDuration() < r.liveTotalMillis())
      _alerter->raiseAlert("task.toolong."+t.id());
  }
  // restart timer for triggers if any was lost, this is never usefull apart
  // if current system time goes back (which btw should never occur on well
  // managed production servers, however it with naive sysops)
  checkTriggersForAllTasks();
}

void Scheduler::propagateTaskInstanceChange(TaskInstance instance) {
  emit itemChanged(instance, nullItem, QStringLiteral("taskinstance"));
}

QHash<quint64,TaskInstance> Scheduler::unfinishedTaskInstances() {
  QHash<quint64,TaskInstance> instances;
  if (this->thread() == QThread::currentThread())
    instances = detachedUnfinishedTaskInstances();
  else
    QMetaObject::invokeMethod(this, [this,&instances](){
      instances = detachedUnfinishedTaskInstances();
    }, Qt::BlockingQueuedConnection);
  return instances;
}

QHash<quint64,TaskInstance> Scheduler::detachedUnfinishedTaskInstances() {
  QHash<quint64,TaskInstance> unfinished = _unfinishedTasks;
  unfinished.detach();
  return unfinished;
}

void Scheduler::shutdown(QDeadlineTimer deadline) {
  if (this->thread() == QThread::currentThread())
    doShutdown(deadline);
  else
    QMetaObject::invokeMethod(this, [this,deadline](){
      doShutdown(deadline);
    }, Qt::BlockingQueuedConnection);
}

void Scheduler::doShutdown(QDeadlineTimer deadline) {
  Log::info() << "shuting down";
  _shutingDown = true;
  BlockingTimer timer(1000);
  while (!deadline.hasExpired()) {
    int remaining = _runningTasks.size();
    if (!remaining)
      break;
    QStringList instanceIds, taskIds;
    for (auto i : _runningTasks.keys()) {
      instanceIds.append(i.id());
      taskIds.append(i.task().id());
    }
    Log::info() << "shutdown : waiting for " << remaining
                << " tasks to finish: " << taskIds << " with ids: "
                << instanceIds;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
    timer.wait();
  }
  QStringList instanceIds, taskIds;
  for (auto i : _queuedTasks) {
    instanceIds.append(i.id());
    taskIds.append(i.task().id());
  }
  Log::info() << "shutdown : leaving " << instanceIds.size()
              << " requests not started on leaving : " << taskIds
              << " with ids: " << instanceIds;
  QThread::usleep(100000);
}

void Scheduler::triggerPlanActions(TaskInstance instance) {
  for (auto subs: instance.task().taskGroup().onplan()
       + instance.task().onplan())
    subs.triggerActions(instance);
}

void Scheduler::triggerStartActions(TaskInstance instance) {
  for (auto subs: config().onstart()
       + instance.task().taskGroup().onstart()
       + instance.task().onstart())
    subs.triggerActions(instance);
}

void Scheduler::triggerFinishActions(
    TaskInstance instance, std::function<bool(Action)> filter) {
  QList<EventSubscription> subs;
  if (instance.success())
    subs = config().onsuccess()
        + instance.task().taskGroup().onsuccess()
        + instance.task().onsuccess();
  else
    subs = config().onfailure()
        + instance.task().taskGroup().onfailure()
        + instance.task().onfailure();
  for (auto es: subs)
    es.triggerActions(instance, filter);
}
