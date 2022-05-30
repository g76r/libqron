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
#include <QThread>
#include "config/configutils.h"
#include "config/requestformfield.h"
#include "trigger/crontrigger.h"
#include "trigger/noticetrigger.h"
#include "thread/blockingtimer.h"
#include "condition/taskwaitcondition.h"
#include "condition/disjunctioncondition.h"
#include "util/paramsprovidermerger.h"
#include "action/action.h"

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
      Executor *executor = new Executor(this);
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
  for (auto instance: detachedUnfinishedTaskInstances()) {
    switch (instance.status()) {
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
    case TaskInstance::Running:
    case TaskInstance::Waiting:
      continue;
    case TaskInstance::Queued:
    case TaskInstance::Planned:
        ;
    }
    QString taskId = instance.task().id();
    Task t = newConfig.tasks().value(taskId);
    if (t.isNull()) {
      doCancelTaskInstance(instance, true,
                           "canceling task instance while reloading "
                           "configuration because this task no longer exists");
    } else {
      Log::info(taskId, instance.id())
          << "replacing task definition in task instance while reloading "
             "configuration";
      instance.setTask(t);
      _unfinishedTasks.insert(instance.idAsLong(), instance);
    }
  }
  _configdate = QDateTime::currentDateTime().toMSecsSinceEpoch();
  if (_firstConfigurationLoad) {
    _firstConfigurationLoad = false;
    Log::info() << "starting scheduler";
    for (auto sub: newConfig.onschedulerstart())
      if (sub.triggerActions())
        break;
  }
  for (auto sub: newConfig.onconfigload())
    if (sub.triggerActions())
      break;
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

void Scheduler::planOrRequestCommonPostProcess(
    TaskInstance instance, TaskInstance herder,
    ParamSet overridingParams) {
  auto params = instance.params();
  auto instanceparams = instance.task().instanceparams();
  auto ppp = instance.pseudoParams();
  for (auto key: instanceparams.keys()) {
    auto rawvalue = instanceparams.rawValue(key);
    auto value = params.evaluate(rawvalue, &ppp);
    instance.setParam(key, ParamSet::escape(value));
  }
  for (auto key: overridingParams.keys()) {
    auto rawvalue = overridingParams.rawValue(key);
    auto value = params.evaluate(rawvalue, &ppp);
    instance.setParam(key, ParamSet::escape(value));
  }
  if (herder.isNull() || herder == instance)
    return;
  _awaitedTasks[herder.idAsLong()] << instance.idAsLong();
  QSet<quint64> &herdedTasks = _unfinishedHerds[herder.idAsLong()];
  herdedTasks << instance.idAsLong();
  herder.appendToHerdedTasksCaption(instance.idSlashId());
  Log::info(herder.task().id(), herder.id())
      << "task appended to herded tasks: " << instance.idSlashId();
}

TaskInstanceList Scheduler::requestTask(
    QString taskId, ParamSet overridingParams, bool force, quint64 herdid) {
  if (this->thread() == QThread::currentThread())
    return doRequestTask(taskId, overridingParams, force, herdid);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
                            force,herdid](){
      instances = doRequestTask(taskId, overridingParams, force, herdid);
    }, Qt::BlockingQueuedConnection);
  return instances;
}

TaskInstanceList Scheduler::doRequestTask(
  QString taskId, ParamSet overridingParams, bool force, quint64 herdid) {
  TaskInstance herder = _unfinishedTasks.value(herdid);
  TaskInstanceList instances;
  Task task = config().tasks().value(taskId);
  if (!planOrRequestCommonPreProcess(taskId, task, overridingParams))
    return instances;
  Cluster cluster = config().clusters().value(task.target());
  if (cluster.balancing() == Cluster::Each) {
    quint64 groupId = 0;
    foreach (Host host, cluster.hosts()) {
      TaskInstance instance(task, groupId, force, overridingParams, herdid);
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
    TaskInstance instance(task, force, overridingParams, herdid);
    instance = enqueueTaskInstance(instance);
    if (!instance.isNull())
      instances.append(instance);
  }
  if (!instances.isEmpty()) {
    for (auto instance: instances) {
      planOrRequestCommonPostProcess(instance, herder, overridingParams);
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
  QString taskId, ParamSet overridingParams, bool force, quint64 herdid,
    Condition queuewhen, Condition cancelwhen) {
  if (this->thread() == QThread::currentThread())
    return doPlanTask(taskId, overridingParams, force, herdid, queuewhen,
                      cancelwhen);
  TaskInstanceList instances;
  QMetaObject::invokeMethod(this, [this,&instances,taskId,overridingParams,
             force,herdid,queuewhen,cancelwhen](){
        instances = doPlanTask(taskId, overridingParams, force, herdid,
                               queuewhen, cancelwhen);
      }, Qt::BlockingQueuedConnection);
  return instances;
}

static Condition guessCancelwhenCondition(
    Condition queuewhen, Condition cancelwhen) {
  if (!cancelwhen.isEmpty())
    return cancelwhen; // keep it if already set
  if (queuewhen.conditionType() == "disjunction") {
    // try to open a condition list and take the only item if size is 1
    auto dc = static_cast<const DisjunctionCondition&>(queuewhen);
    if (dc.size() != 1)
      return cancelwhen;
    queuewhen = dc.conditions().first();
  }
  if (queuewhen.conditionType() == "taskwait") {
    // if TaskWaitCondition, use conjugate operator
    auto twc = static_cast<const TaskWaitCondition&>(queuewhen);
    TaskWaitOperator op =
        TaskWaitCondition::cancelOperatorFromQueueOperator(twc.op());
    //qDebug() << "guessing cancel condition from queue condition: "
    //         << twc.toPfNode().toString()
    //         << TaskWaitCondition::operatorAsString(twc.op()) << twc.expr()
    //         << "->" << TaskWaitCondition::operatorAsString(op);
    return TaskWaitCondition(op, twc.expr());
  }
  return cancelwhen;
}

TaskInstanceList Scheduler::doPlanTask(
  QString taskId, ParamSet overridingParams, bool force, quint64 herdid,
  Condition queuewhen, Condition cancelwhen) {
  TaskInstance herder = _unfinishedTasks.value(herdid);
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
  if (herder.isNull()) { // no herder -> no conditions
    if (!queuewhen.isEmpty()) {
      Log::warning() << "ignoring queuewhen condition when planning a task out"
                        " of any herd : " << queuewhen.toString();
    }
    if (!cancelwhen.isEmpty()) {
      Log::warning() << "ignoring cancelwhen condition when planning a task out"
                        " of any herd : " << cancelwhen.toString();
    }
    queuewhen = Condition();
    cancelwhen = Condition();
  }
  /* default queuewhen condition is allstarted %!parenttaskinstanceid
   * rather than (true), which means "when parent is started"
   * naive users can stop reading here, but...
   * actualy it means "when parent is started" if parent is an intermediary task
   * because %!parenttaskinstanceid will be resolved to its taskinstanceid
   * and the same if parent is the herder but for another reason: because the
   * herder taskinstanceid is not in it's herded task list and is thus ignored
   * by allstarted condition which is always true but won't be evaluated...
   * before the herder (hence the parent) starts */
  if (queuewhen.isEmpty() && !herder.isNull())
    queuewhen = TaskWaitCondition(TaskWaitOperator::AllStarted,
                                  "%!parenttaskinstanceid");

  if (cancelwhen.isEmpty())
    cancelwhen = guessCancelwhenCondition(queuewhen, cancelwhen);
  TaskInstance instance(task, force, overridingParams, herdid, queuewhen,
                        cancelwhen);
  _unfinishedTasks.insert(instance.idAsLong(), instance);
  Log::debug(taskId, instance.idAsLong())
      << "planning task " << instance.idSlashId() << " "
      << overridingParams << " with herdid " << instance.herdid()
      << " and queue condition " << instance.queuewhen().toString()
      << " and cancel condition " << instance.cancelwhen().toString();
  planOrRequestCommonPostProcess(instance, herder, overridingParams);
  if (herder.isNull()) {
    emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
    triggerPlanActions(instance);
    instance = enqueueTaskInstance(instance);
  } else {
    triggerPlanActions(instance);
    reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
  }
  emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
  if (!herder.isNull())
    emit itemChanged(herder, herder, QStringLiteral("taskinstance"));
  Log::debug(taskId, instance.idAsLong())
      << "task instance params after planning" << instance.params();
  if (!instance.isNull())
    instances.append(instance);
  return instances;
}

TaskInstance Scheduler::enqueueTaskInstance(TaskInstance instance) {
  auto task = instance.task();
  auto taskId = task.id();
  auto params = instance.params();
  auto ppp = instance.pseudoParams();
  if (_shutingDown) {
    Log::warning(taskId, instance.idAsLong())
        << "cannot queue task because scheduler is shuting down";
    return TaskInstance();
  }
  if (!instance.force()) {
    if (!task.enabled()) {
      doCancelTaskInstance(instance, false,
                           "canceling task because it is disabled : "
                               + instance.idSlashId());
      return TaskInstance();
    }
    auto mqi = params.evaluate(task.maxQueuedInstances(), &ppp).toInt();
    if (mqi > 0) {
      auto criterion = task.deduplicateCriterion();
      auto newcrit = params.evaluate(criterion, &ppp);
      QList<TaskInstance> others;
      for (auto other: _unfinishedTasks) {
        if (other.status() != TaskInstance::Queued)
          continue;
        if (other.task().id() != taskId)
          continue;
        if (other.groupId() == instance.groupId())
          continue;
        auto ppp = other.pseudoParams();
        auto params = other.params();
        auto othercrit = params.evaluate(criterion, &ppp);
        if (othercrit == newcrit)
          others.append(other);
      }
      int canceled = 0;
      while (others.size() > mqi-1) {
        auto other = others.takeLast();
        doCancelTaskInstance(
            other, true,
            "canceling task because another instance of the same task is "
            "queued : " + instance.idSlashId()
            + " with same deduplicate criterion : " + newcrit);
        ++canceled;
      }
    }
  }
  qint64 queueSize = 0;
  for (auto i: _unfinishedTasks)
    if (i.status() == TaskInstance::Queued)
      ++queueSize;
  if (queueSize >= config().maxqueuedrequests()) {
    Log::error(taskId, instance.idAsLong())
        << "cannot queue task because maxqueuedrequests is already reached ("
        << config().maxqueuedrequests() << ")";
    _alerter->raiseAlert("scheduler.maxqueuedrequests.reached");
    return TaskInstance();
  }
  instance.setQueueDatetime();
  _unfinishedTasks.insert(instance.idAsLong(), instance);
  _alerter->cancelAlert("scheduler.maxqueuedrequests.reached");
  Log::debug(taskId, instance.idAsLong())
      << "queuing task " << instance.idSlashId()
      << " with request group id " << instance.groupId()
      << " and herdid " << instance.herdid();
  // note: a request must always be queued even if the task can be started
  // immediately, to avoid the new tasks being started before queued ones
  ++queueSize;
  if (queueSize > _queuedTasksHwm)
    _queuedTasksHwm = queueSize;
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

void Scheduler::cancelOrAbortHerdedTasks(
    TaskInstance herder, bool allowEvenAbort) {
  auto herdedTasks = this->herdedTasks(herder.idAsLong());
  for (auto instance: herdedTasks) {
    switch (instance.status()) {
    case TaskInstance::Planned:
    case TaskInstance::Queued:
      doCancelTaskInstance(
          instance, false, "canceling task because herder task was canceled : "
                               + herder.idSlashId());
      break;
    case TaskInstance::Running:
      if (allowEvenAbort)
        doAbortTaskInstance(instance.idAsLong());
      break;
    case TaskInstance::Waiting:
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
      break;
    }
  }
}

TaskInstance Scheduler::doCancelTaskInstance(
    TaskInstance instance, bool warning, const char *reason) {
  switch (instance.status()) {
  case TaskInstance::Planned:
  case TaskInstance::Queued:
    cancelOrAbortHerdedTasks(instance, false);
    if (warning)
      Log::warning(instance.task().id(), instance.id()) << reason;
    else
      Log::info(instance.task().id(), instance.id()) << reason;
    taskInstanceStoppedOrCanceled(instance, 0, false);
    return instance;
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
  for (auto i: _unfinishedTasks) {
    switch (i.status()) {
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
    case TaskInstance::Running:
    case TaskInstance::Waiting:
      continue;
    case TaskInstance::Queued:
    case TaskInstance::Planned:
        ;
    }
    if (i.task().id() == taskId)
      instances << i;
  }
  for (auto instance : instances)
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
  for (auto instance: detachedUnfinishedTaskInstances()) {
    if (id != instance.idAsLong())
      continue;
    auto taskId = instance.task().id();
    auto executor = _runningExecutors.value(id);
    switch (instance.status()) {
    case TaskInstance::Running:
    case TaskInstance::Waiting:
      cancelOrAbortHerdedTasks(instance, true);
      if (executor) {
        Log::warning(taskId, id) << "aborting running task as requested";
        // TODO should return TaskInstance() if executor cannot actually abort
        executor->abort();
      }
      return instance;
    case TaskInstance::Planned:
    case TaskInstance::Queued:
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
      break;
    }
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
  for (auto ti: detachedUnfinishedTaskInstances()) {
    if (ti.task().id() != taskId)
      continue;
    if (_runningExecutors.contains(ti.idAsLong())) {
      ti = doAbortTaskInstance(ti.idAsLong());
      if (!ti.isNull())
        instances << ti;
    }
  }
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
    TaskInstanceList instances = requestTask(taskId, overridingParams);
    if (!instances.isEmpty())
      for (auto ti : instances)
        Log::info(taskId, ti.idAsLong())
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
  params.setValue(QStringLiteral("!notice"), notice);
  Log::info() << "posting notice " << notice << " with params " << params;
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
        TaskInstanceList instances = requestTask(task.id(), overridingParams);
        if (!instances.isEmpty())
          for (auto ti: instances)
            Log::info(task.id(), ti.idAsLong())
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
  ParamsProviderMerger ppm(params);
  for (auto sub: config().onnotice())
    if (sub.triggerActions(&ppm, TaskInstance()))
      break;
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

QSet<TaskInstance> Scheduler::herdedTasks(
  quint64 herdid, bool includeFinished) {
  QSet<TaskInstance> tasks;
  for (auto id: includeFinished ? _unfinishedHerds.value(herdid)
                                : _awaitedTasks.value(herdid)) {
    auto i = _unfinishedTasks.value(id);
    if (!i.isNull())
      tasks << i;
  }
  return tasks;
}

void Scheduler::enqueueAsManyTaskInstancesAsPossible() {
  if (_shutingDown)
    return;
  auto dirtyHerds = _dirtyHerds;
  _dirtyHerds.clear();
  for (auto instance: detachedUnfinishedTaskInstances()) {
    if (!dirtyHerds.contains(instance.herdid()))
      continue;
    if (instance.status() != TaskInstance::Planned)
      continue;
    auto herder = _unfinishedTasks.value(instance.herdid());
    if (herder.status() == TaskInstance::Planned || // should never happen
        herder.status() == TaskInstance::Queued) // e.g. waiting for resource
      continue;
    QSet<TaskInstance> herdedTasks = this->herdedTasks(instance.herdid());
    Condition queuewhen = instance.queuewhen();
    if (queuewhen.evaluate(instance, herder, herdedTasks)) {
      Log::info(instance.task().id(), instance.id())
          << "queuing task because queue condition is met: "
          << queuewhen.toString();
      instance = enqueueTaskInstance(instance);
      if (!instance.isNull()) [[likely]]
        emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
      continue;
    }
    Condition cancelwhen = instance.cancelwhen();
    if (cancelwhen.evaluate(instance, herder, herdedTasks)) {
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
  for (auto instance: detachedUnfinishedTaskInstances()) {
    if (instance.status() != TaskInstance::Queued)
      continue; // not queued, or even changed meanwhile, e.g. canceled
    auto task = instance.task();
    startTaskInstance(instance);
  }
}

void Scheduler::startTaskInstance(TaskInstance instance) {
  Task task(instance.task());
  QString taskId = task.id();
  Executor *executor = 0;
  if (!task.enabled())
    return; // do not start disabled tasks
  if (_availableExecutors.isEmpty() && !instance.force()) {
    QString s = "cannot execute task '"+taskId
                +"' now because there are already too many tasks running "
                  "(maxtotaltaskinstances reached) currently running tasks: ";
    QDateTime now = QDateTime::currentDateTime();
    for (auto tii: _runningExecutors.keys()) {
      auto ti = _unfinishedTasks.value(tii);
      s += ti.id()+" "+ti.task().id()+" since "
           +QString::number(ti.startDatetime().msecsTo(now))+" ms; ";
    }
    Log::info(taskId, instance.idAsLong()) << s;
    _alerter->raiseAlert("scheduler.maxtotaltaskinstances.reached");
    return;
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
    return;
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
    return;
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
      executor = new Executor(this);
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
    _runningExecutors.insert(instance.idAsLong(), executor);
    if (_runningExecutors.size() > _runningTasksHwm)
      _runningTasksHwm = _runningExecutors.size();
    triggerStartActions(instance);
    reevaluateQueuedTaskInstances();
    return;
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
  return;
}

void Scheduler::taskInstanceStoppedOrCanceled(
    TaskInstance instance, Executor *executor, bool markCanceledAsFailure) {
  if (executor) {
    if (executor->isTemporary())
      executor->deleteLater();
    else
      _availableExecutors.append(executor);
  }
  _runningExecutors.remove(instance.idAsLong());
  auto herder = _unfinishedTasks.value(instance.herdid());
  if (!instance.isHerder()) {
    taskInstanceFinishedOrCanceled(instance, markCanceledAsFailure);
    if (_awaitedTasks.contains(herder.idAsLong())) {
      QSet<quint64> &awaitedTasks = _awaitedTasks[herder.idAsLong()];
      awaitedTasks.remove(instance.idAsLong());
      Log::info(herder.task().id(), herder.id())
          << "herded task finished: " << instance.idSlashId()
          << " remaining: " << awaitedTasks.size() << " : " << awaitedTasks;
      if (awaitedTasks.isEmpty() && herder.status() == TaskInstance::Waiting)
        taskInstanceFinishedOrCanceled(herder, false);
      else
        reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
    }
    return;
  }
  if (instance.status() != TaskInstance::Canceled) {
    // must trigger actions that may create new herded tasks now, to be
    // able to wait for them
    triggerFinishActions(instance, [](Action a) {
      return a.mayCreateTaskInstances();
    });
  }
  // configured and requested tasks are different if config was reloaded
  Task configuredTask = config().tasks().value(instance.task().id());
  auto waitingTasks = _awaitedTasks[instance.idAsLong()];
  if (instance.status() == TaskInstance::Canceled || waitingTasks.isEmpty()
      || configuredTask.herdingPolicy() == Task::NoWait) {
    taskInstanceFinishedOrCanceled(instance, markCanceledAsFailure);
    return;
  }
  Log::info(instance.task().id(), instance.id())
      << "waiting for herded tasks to finish: " << waitingTasks;
  instance.setAbortable();
  emit itemChanged(instance, instance, QStringLiteral("taskinstance"));
  reevaluatePlannedTaskInstancesForHerd(herder.idAsLong());
}

static void recomputeHerderSuccess(
  TaskInstance &herder, QSet<TaskInstance> herdedTasks) {
  if (herdedTasks.isEmpty())
    return; // keep own status
  switch(herder.task().herdingPolicy()) {
    case Task::AllSuccess:
      if (!herder.success())
        return;
      for (auto sheep: herdedTasks)
        if (sheep.status() != TaskInstance::Success) {
          herder.setSuccess(false);
          return;
        }
      return;
    case Task::NoFailure:
      if (!herder.success())
        return;
      for (auto sheep: herdedTasks)
        if (sheep.status() == TaskInstance::Failure) {
          herder.setSuccess(false);
          return;
        }
      return;
    case Task::OneSuccess:
      herder.setSuccess(false);
      for (auto sheep: herdedTasks)
        if (sheep.status() == TaskInstance::Success) {
          herder.setSuccess(true);
          return;
        }
      return;
    case Task::OwnStatus:
    case Task::NoWait:
    case Task::HerdingPolicyUnknown: // should never happen
      return;
  }
}

void Scheduler::taskInstanceFinishedOrCanceled(
    TaskInstance instance, bool markCanceledAsFailure) {
  Task requestedTask = instance.task();
  QString taskId = requestedTask.id();
  // configured and requested tasks are different if config was reloaded
  Task configuredTask = config().tasks().value(taskId);
  auto now = QDateTime::currentDateTime();
  instance.setFinishDatetime(now);
  if (instance.status() == TaskInstance::Canceled) { // i.e. start date not set
    instance.setReturnCode(-1);
    instance.setSuccess(false);
    if (markCanceledAsFailure)
      instance.setStartDatetime(now);
    instance.setStopDatetime(now);
    if (instance.task().runningCount() < instance.task().maxInstances())
      _alerter->cancelAlert("task.maxinstancesreached."+taskId);
  } else {
    auto herdedTasks = this->herdedTasks(instance.idAsLong());
    recomputeHerderSuccess(instance, herdedTasks);
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
    configuredTask.setLastDurationMillis((int)instance.durationMillis());
    configuredTask.setLastTaskInstanceId(instance.idAsLong());
    triggerFinishActions(instance, [instance](Action a) {
      return instance.idAsLong() != instance.herdid()
          || !a.mayCreateTaskInstances();
    });
    if (configuredTask.maxExpectedDuration() < LLONG_MAX) {
      if (configuredTask.maxExpectedDuration() < instance.durationMillis())
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
  }
  _awaitedTasks.remove(instance.idAsLong());
  // forget herded tasks only when the whole herd is finished
  if (instance.herdid() == instance.idAsLong()) {
    _unfinishedTasks.remove(instance.idAsLong());
    for (auto i: _unfinishedHerds.value(instance.idAsLong()))
      _unfinishedTasks.remove(i);
  }
  _unfinishedHerds.remove(instance.idAsLong());
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
    _alerter->cancelAlert("task.disabled."+taskId);
  else
    _alerter->raiseAlert("task.disabled."+taskId);
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
  for (auto i: _unfinishedTasks) {
    switch (i.status()) {
    case TaskInstance::Planned:
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
      continue;
    case TaskInstance::Queued:
    case TaskInstance::Running:
    case TaskInstance::Waiting:
        ;
    }
    auto t = i.task();
    if (t.maxExpectedDuration() < i.liveDurationMillis())
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

QMap<quint64,TaskInstance> Scheduler::unfinishedTaskInstances() {
  QMap<quint64,TaskInstance> instances;
  if (this->thread() == QThread::currentThread())
    instances = detachedUnfinishedTaskInstances();
  else
    QMetaObject::invokeMethod(this, [this,&instances](){
      instances = detachedUnfinishedTaskInstances();
    }, Qt::BlockingQueuedConnection);
  return instances;
}

QMap<quint64,TaskInstance> Scheduler::detachedUnfinishedTaskInstances() {
  auto unfinished = _unfinishedTasks;
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
    int remaining = _runningExecutors.size();
    if (!remaining)
      break;
    QStringList instanceIds, taskIds;
    for (auto tii : _runningExecutors.keys()) {
      auto ti = _unfinishedTasks.value(tii);
      instanceIds.append(ti.id());
      taskIds.append(ti.task().id());
    }
    Log::info() << "shutdown : waiting for " << remaining
                << " tasks to finish: " << taskIds << " with ids: "
                << instanceIds;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
    timer.wait();
  }
  QStringList instanceIds, taskIds;
  for (auto i: _unfinishedTasks) {
    switch (i.status()) {
    case TaskInstance::Success:
    case TaskInstance::Failure:
    case TaskInstance::Canceled:
    case TaskInstance::Running:
    case TaskInstance::Waiting:
      continue;
    case TaskInstance::Queued:
    case TaskInstance::Planned:
        ;
    }
    instanceIds.append(i.id());
    taskIds.append(i.task().id());
  }
  Log::info() << "shutdown : leaving " << instanceIds.size()
              << " requests not started on leaving : " << taskIds
              << " with ids: " << instanceIds;
  QThread::usleep(100000);
}

void Scheduler::triggerPlanActions(TaskInstance instance) {
  auto ppp = instance.pseudoParams();
  auto ppm = ParamsProviderMerger(&ppp)(instance.params());
  for (auto subs: config().tasksRoot().onplan()
       + instance.task().taskGroup().onplan()
       + instance.task().onplan())
    if (subs.triggerActions(&ppm, instance))
      break;
}

void Scheduler::triggerStartActions(TaskInstance instance) {
  auto ppp = instance.pseudoParams();
  auto ppm = ParamsProviderMerger(&ppp)(instance.params());
  for (auto subs: config().tasksRoot().onstart()
       + instance.task().taskGroup().onstart()
       + instance.task().onstart())
    if (subs.triggerActions(&ppm, instance))
      break;
}

void Scheduler::triggerFinishActions(
    TaskInstance instance, std::function<bool(Action)> filter) {
  QList<EventSubscription> subs;
  if (instance.success())
    subs = config().tasksRoot().onsuccess()
        + instance.task().taskGroup().onsuccess()
        + instance.task().onsuccess();
  else
    subs = config().tasksRoot().onfailure()
        + instance.task().taskGroup().onfailure()
        + instance.task().onfailure();
  auto ppp = instance.pseudoParams();
  auto ppm = ParamsProviderMerger(&ppp)(instance.params());
  for (auto es: subs)
    if (es.triggerActions(&ppm, instance, filter))
      break;
}

void Scheduler::taskInstanceParamAppend(
  quint64 taskinstanceid, QString key, QString value) {
  if (this->thread() == QThread::currentThread())
    doTaskInstanceParamAppend(taskinstanceid, key, value);
  QMetaObject::invokeMethod(this, [this,taskinstanceid,key,value](){
      doTaskInstanceParamAppend(taskinstanceid, key, value);
    }, Qt::QueuedConnection); // don't wait for return, async !
}

void Scheduler::doTaskInstanceParamAppend(
  quint64 taskinstanceid, QString key, QString value) {
  if (!_unfinishedTasks.contains(taskinstanceid))
    return;
  _unfinishedTasks[taskinstanceid].paramAppend(key, value);
}
