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
#include "taskinstance.h"
#include <QSharedData>
#include <QDateTime>
#include <QAtomicInt>
#include "format/timeformats.h"
#include <functional>
#include "util/radixtree.h"
#include "thread/atomicvalue.h"
#include "condition/disjunctioncondition.h"

static QString _uiHeaderNames[] = {
  "Instance Id", // 0
  "Task Id",
  "Status",
  "Creation Date",
  "Start Date",
  "Stop Date", // 5
  "Time queued",
  "Time running",
  "Actions",
  "Abortable",
  "Herd Id", // 10
  "Herded Task Instances",
  "Finish Date",
  "Time waiting",
  "Duration",
  "Queue date", // 15
  "Time planned",
  "Queue when",
  "Cancel when", // 18
};

static QAtomicInt _sequence;

class TaskInstanceData : public SharedUiItemData {
public:
  quint64 _id, _groupId;
  QString _idAsString;
  Task _task;
  mutable AtomicValue<ParamSet> _params;
  QDateTime _creationDateTime;
  bool _force;
  TaskInstance _herder;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _queue, _start, _stop, _finish;
  mutable bool _success;
  mutable int _returnCode;
  // note: Host is not thread-safe either, however setTarget() is not likely to
  // be called without at less one other reference of the Host object, therefore
  // ~Host() would never be called within setTarget() context which would make
  // it thread-safe.
  // There may be a possibility if configuration reload or ressource management
  // or any other Host change occurs. Therefore in addition setTarget() makes a
  // deep copy (through Host.detach()) of original object, so there are no race
  // conditions as soon as target() return value is never modified, which should
  // be the case forever.
  mutable Host _target;
  mutable bool _abortable;
  mutable AtomicValue<TaskInstanceList> _herdedTasks;
  Condition _queuewhen, _cancelwhen;

  TaskInstanceData(Task task, ParamSet params, bool force,
                   TaskInstance herder = TaskInstance(), quint64 groupId = 0,
                   Condition queuewhen = Condition(),
                   Condition cancelwhen = Condition())
    : _id(newId()), _groupId(groupId ? groupId : _id),
      _idAsString(QString::number(_id)),
      _task(task), _params(params),
      _creationDateTime(QDateTime::currentDateTime()), _force(force),
      _herder(herder), _queue(LLONG_MIN), _start(LLONG_MIN), _stop(LLONG_MIN),
      _finish(LLONG_MIN),
      _success(false), _returnCode(0), _abortable(false), _queuewhen(queuewhen),
      _cancelwhen(cancelwhen) {
    auto p = _params.lockedData();
    p->setParent(task.params());
  }
  TaskInstanceData()
    : _id(0), _groupId(0), _force(false),
      _queue(LLONG_MIN), _start(LLONG_MIN), _stop(LLONG_MIN),
      _finish(LLONG_MIN), _success(false), _returnCode(0),
      _abortable(false) { }

private:
  static quint64 newId() {
    QDateTime now = QDateTime::currentDateTime();
    return now.date().year() * 100000000000000LL
        + now.date().month() * 1000000000000LL
        + now.date().day() * 10000000000LL
        + now.time().hour() * 100000000LL
        + now.time().minute() * 1000000LL
        + now.time().second() * 10000LL
        + _sequence.fetchAndAddOrdered(1)%10000;
  }

public:
  QString id() const override { return _idAsString; }
  QString idQualifier() const override { return "taskinstance"; }
  int uiSectionCount() const override;
  QVariant uiData(int section, int role) const override;
  QVariant uiHeaderData(int section, int role) const override;
  QDateTime inline creationDatetime() const { return _creationDateTime; }
  QDateTime inline queueDatetime() const { return _queue != LLONG_MIN
        ? QDateTime::fromMSecsSinceEpoch(_queue) : QDateTime(); }
  void inline setQueueDatetime(QDateTime datetime) const {
    _queue = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN; }
  QDateTime inline startDatetime() const { return _start != LLONG_MIN
        ? QDateTime::fromMSecsSinceEpoch(_start) : QDateTime(); }
  void inline setStartDatetime(QDateTime datetime) const {
    _start = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN; }
  QDateTime inline stopDatetime() const { return _stop != LLONG_MIN
        ? QDateTime::fromMSecsSinceEpoch(_stop) : QDateTime(); }
  void inline setStopDatetime(QDateTime datetime) const {
    _stop = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN; }
  QDateTime inline finishDatetime() const { return _finish != LLONG_MIN
        ? QDateTime::fromMSecsSinceEpoch(_finish) : QDateTime(); }
  void inline setFinishDatetime(QDateTime datetime) const {
    _finish = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN; }
  qint64 inline plannedMillis() const {
    return _creationDateTime.msecsTo(queueDatetime()); }
  qint64 inline queuedMillis() const {
    return _queue != LLONG_MIN && _start != LLONG_MIN ? _start - _queue : 0; }
  qint64 inline runningMillis() const {
    return _start != LLONG_MIN && _stop != LLONG_MIN ? _stop - _start : 0; }
  qint64 inline waitingMillis() const {
    return _stop != LLONG_MIN && _finish != LLONG_MIN ? _finish - _stop : 0; }
  qint64 inline durationMillis() const {
    return _queue != LLONG_MIN && _finish != LLONG_MIN ? _finish - _queue : 0; }
  qint64 inline liveDurationMillis() const {
    if (_queue == LLONG_MIN) // still planned
      return 0;
    if (_finish == LLONG_MIN) // not finished : taking live value so far
      return QDateTime::currentMSecsSinceEpoch() - _queue;
    return _finish - _queue; // regular durationMillis()
  }
  TaskInstance::TaskInstanceStatus inline status() const {
    if (_finish != LLONG_MIN) {
      if (_start == LLONG_MIN)
        return TaskInstance::Canceled;
      return _success ? TaskInstance::Success : TaskInstance::Failure;
    }
    if (_stop != LLONG_MIN) {
      if (_start == LLONG_MIN) // should never happen b/c _finish should be set
        return TaskInstance::Canceled;
      return TaskInstance::Waiting;
    }
    if (_start != LLONG_MIN)
      return TaskInstance::Running;
    if (_queue != LLONG_MIN)
      return TaskInstance::Queued;
    return TaskInstance::Planned;
  }
};

TaskInstance::TaskInstance() {
}

TaskInstance::TaskInstance(const TaskInstance &other) : SharedUiItem(other) {
}

TaskInstance::TaskInstance(
    Task task, bool force, ParamSet params, TaskInstance herder,
    Condition queuewhen, Condition cancelwhen)
  : SharedUiItem(new TaskInstanceData(
          task, params, force, herder, 0, queuewhen, cancelwhen)) {
}

TaskInstance::TaskInstance(Task task, quint64 groupId,
                           bool force,
                           ParamSet params, TaskInstance herder)
  : SharedUiItem(new TaskInstanceData(task, params, force, herder, groupId)) {
}

Task TaskInstance::task() const {
  const TaskInstanceData *d = data();
  return d ? d->_task : Task();
}

void TaskInstance::setParam(QString key, QString value) const {
  auto d = data();
  if (!d)
    return;
  auto params = d->_params.lockedData();
  params->setValue(key, value);
}

void TaskInstance::paramAppend(QString key, QString value) const {
  auto d = data();
  if (!d)
    return;
  auto params = d->_params.lockedData();
  auto current = params->rawValue(key);
  if (current.isEmpty())
    params->setValue(key, value);
  else
    params->setValue(key, current+' '+value);
}

ParamSet TaskInstance::params() const {
  const TaskInstanceData *d = data();
  return d ? d->_params.detachedData() : ParamSet();
}

quint64 TaskInstance::idAsLong() const {
  const TaskInstanceData *d = data();
  return d ? d->_id : 0;
}

quint64 TaskInstance::groupId() const {
  const TaskInstanceData *d = data();
  return d ? d->_groupId : 0;
}

QDateTime TaskInstance::creationDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->creationDatetime() : QDateTime();
}

QDateTime TaskInstance::queueDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->queueDatetime() : QDateTime();
}

void TaskInstance::setQueueDatetime(QDateTime datetime) const {
  const TaskInstanceData *d = data();
  if (d)
    d->setQueueDatetime(datetime);
}

QDateTime TaskInstance::startDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->startDatetime() : QDateTime();
}

void TaskInstance::setStartDatetime(QDateTime datetime) const {
  const TaskInstanceData *d = data();
  if (d)
    d->setStartDatetime(datetime);
}

QDateTime TaskInstance::stopDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->stopDatetime() : QDateTime();
}

void TaskInstance::setStopDatetime(QDateTime datetime) const {
  const TaskInstanceData *d = data();
  if (d)
    d->setStopDatetime(datetime);
}

QDateTime TaskInstance::finishDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->finishDatetime() : QDateTime();
}

void TaskInstance::setFinishDatetime(QDateTime datetime) const {
  const TaskInstanceData *d = data();
  if (d)
    d->setFinishDatetime(datetime);
}

qint64 TaskInstance::plannedMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->plannedMillis() : 0;
}

qint64 TaskInstance::queuedMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->queuedMillis() : 0;
}

qint64 TaskInstance::runningMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->runningMillis() : 0;
}

qint64 TaskInstance::waitingMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->waitingMillis() : 0;
}

qint64 TaskInstance::durationMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->durationMillis() : 0;
}

qint64 TaskInstance::liveDurationMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->liveDurationMillis() : 0;
}

TaskInstance::TaskInstanceStatus TaskInstance::status() const {
  const TaskInstanceData *d = data();
  return d ? d->status() : Failure;
}

bool TaskInstance::success() const {
  const TaskInstanceData *d = data();
  return d ? d->_success : false;
}

void TaskInstance::setSuccess(bool success) const {
  const TaskInstanceData *d = data();
  if (d)
    d->_success = success;
}

void TaskInstance::setHerderSuccess(Task::HerdingPolicy herdingpolicy) const {
  const TaskInstanceData *d = data();
  if (!d)
    return;
  auto sheeps = d->_herdedTasks.lockedData();
  //qDebug() << "setHerderSuccess" << d->_task.id() << d->_id << sheeps->join(' ')
  //         << d->_success << Task::herdingPolicyAsString(herdingpolicy);
  if (sheeps->isEmpty())
    return; // keep own status
  switch(herdingpolicy) {
  case Task::AllSuccess:
    if (!d->_success)
      return;
    //qDebug() << "sheeps";
    for (auto sheep: *sheeps)
      if (sheep.status() != TaskInstance::Success) {
        //qDebug() << "not success" << sheep.id();
        d->_success = false;
        return;
      }
    return;
  case Task::NoFailure:
    if (!d->_success)
      return;
    //qDebug() << "sheeps";
    for (auto sheep: *sheeps)
      if (sheep.status() == TaskInstance::Failure) {
        //qDebug() << "failure" << sheep.id();
        d->_success = false;
        return;
      }
    return;
  case Task::OneSuccess:
    d->_success = false;
    for (auto sheep: *sheeps)
      if (sheep.status() == TaskInstance::Success) {
        d->_success = true;
        return;
      }
    return;
  case Task::OwnStatus:
  case Task::NoWait:
  case Task::HerdingPolicyUnknown: // should never happen
    return;
  }
}

int TaskInstance::returnCode() const {
  const TaskInstanceData *d = data();
  return d ? d->_returnCode : -1;
}

void TaskInstance::setReturnCode(int returnCode) const {
  const TaskInstanceData *d = data();
  if (d)
    d->_returnCode = returnCode;
}

Host TaskInstance::target() const {
  const TaskInstanceData *d = data();
  return d ? d->_target : Host();
}

void TaskInstance::setTarget(Host target) const {
  const TaskInstanceData *d = data();
  if (d) {
    target.detach();
    d->_target = target;
  }
}

static RadixTree<std::function<QVariant(
    const TaskInstance&, const QString&)>> _pseudoParams {
{ "!taskinstanceid" , [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.id();
} },
{ "!herdid", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herdid();
} },
{ "!taskinstancegroupid", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.groupId();
} },
{ "!runningms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.runningMillis();
} },
{ "!herdrunningms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().runningMillis();
} },
{ "!runnings", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.runningMillis()/1000;
} },
{ "!herdrunnings", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().runningMillis()/1000;
} },
{ "!waitingms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.waitingMillis();
} },
{ "!waitings", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.waitingMillis()/1000;
} },
{ "!herdwaitingms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().waitingMillis();
} },
{ "!herdwaitings", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().waitingMillis()/1000;
} },
{ "!plannedms", [](const TaskInstance &taskInstance, const QString&) {
   return taskInstance.plannedMillis();
 } },
{ "!planneds", [](const TaskInstance &taskInstance, const QString&) {
   return taskInstance.plannedMillis()/1000;
 } },
{ "!queuedms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.queuedMillis();
} },
{ "!herdqueuedms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().queuedMillis();
} },
{ "!queueds", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.queuedMillis()/1000;
} },
{ "!herdqueueds", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().queuedMillis()/1000;
} },
{ "!durationms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.durationMillis();
} },
{ "!herddurationms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().durationMillis();
} },
{ "!durations", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.durationMillis()/1000;
} },
{ "!herddurations", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().durationMillis()/1000;
} },
{ "!returncode", [](const TaskInstance &taskInstance, const QString&) {
  return QString::number(taskInstance.returnCode());
} },
{ "!status", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.statusAsString();
} },
{ "!target", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.target().id();
} },
{ "!targethostname", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.target().hostname();
} },
{ "!requestdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.creationDatetime(), key.mid(15));
}, true },
{ "!herdrequestdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.herder().creationDatetime(), key.mid(19));
}, true },
{ "!startdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.startDatetime(), key.mid(10));
}, true },
{ "!herdstartdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.herder().startDatetime(), key.mid(14));
}, true },
{ "!stopdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.stopDatetime(), key.mid(9));
}, true },
{ "!herdstopdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.herder().stopDatetime(), key.mid(13));
}, true },
{ "!finishdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.finishDatetime(), key.mid(11));
}, true },
{ "!herdfinishdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.herder().finishDatetime(), key.mid(15));
}, true },
};

QVariant TaskInstancePseudoParamsProvider::paramValue(
    QString key, const ParamsProvider *context, QVariant defaultValue,
    QSet<QString> alreadyEvaluated) const {
  auto pseudoParam = _pseudoParams.value(key);
  if (pseudoParam)
    return pseudoParam(_taskInstance, key);
  return _taskPseudoParams.paramValue(key, context, defaultValue, alreadyEvaluated);
}

QSet<QString> TaskInstancePseudoParamsProvider::keys() const {
  return _taskPseudoParams.keys()+_pseudoParams.keys();
}

void TaskInstance::setTask(Task task) {
  TaskInstanceData *d = data();
  if (d)
    d->_task = task;
}

bool TaskInstance::force() const {
  const TaskInstanceData *d = data();
  return d ? d->_force : false;
}

TaskInstance TaskInstance::herder() const {
  const TaskInstanceData *d = data();
  return d && !d->_herder.isNull() ? d->_herder : *this;
}

TaskInstanceList TaskInstance::herdedTasks() const {
  const TaskInstanceData *d = data();
  if (!d)
    return TaskInstanceList();
  return d->_herdedTasks.detachedData();
}

void TaskInstance::appendHerdedTask(TaskInstance sheep) const {
  const TaskInstanceData *d = data();
  if (!d || sheep.idAsLong() == idAsLong())
    return;
  auto sheeps = d->_herdedTasks.lockedData();
  if (!sheeps->contains(sheep))
    sheeps->append(sheep);
}

Condition TaskInstance::queuewhen() const {
  const TaskInstanceData *d = data();
  return d ? d->_queuewhen : Condition();
}

Condition TaskInstance::cancelwhen() const {
  const TaskInstanceData *d = data();
  return d ? d->_cancelwhen : Condition();
}

static QHash<TaskInstance::TaskInstanceStatus,QString> _statuses {
  { TaskInstance::Planned, "planned" },
  { TaskInstance::Queued, "queued" },
  { TaskInstance::Running, "running" },
  { TaskInstance::Waiting, "waiting" },
  { TaskInstance::Success, "success" },
  { TaskInstance::Failure, "failure" },
  { TaskInstance::Canceled, "canceled" },
};

QString TaskInstance::statusAsString(TaskInstance::TaskInstanceStatus status) {
  return _statuses.value(status, QStringLiteral("unknown"));
}

bool TaskInstance::abortable() const {
  const TaskInstanceData *d = data();
  return d && d->_abortable;
}

void TaskInstance::setAbortable(bool abortable) const {
  const TaskInstanceData *d = data();
  if (d)
    d->_abortable = abortable;
}

QVariant TaskInstanceData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int TaskInstanceData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

QVariant TaskInstanceData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
    switch(section) {
    case 0:
      return _idAsString;
    case 1:
      return _task.id();
    case 2:
      return TaskInstance::statusAsString(status());
    case 3:
      return creationDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 4:
      return startDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 5:
      return stopDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 6:
      return startDatetime().isNull() || queueDatetime().isNull()
          ? QVariant() : QString::number(queuedMillis()/1000.0);
    case 7:
      return stopDatetime().isNull() || startDatetime().isNull()
          ? QVariant() : QString::number(runningMillis()/1000.0);
    case 8:
      return QVariant(); // custom actions, handled by the model, if needed
    case 9:
      return _abortable;
    case 10:
      return _herder.isNull() ? _idAsString : _herder.id();
    case 11:
      return _herdedTasks.detachedData().operator QStringList().join(' ');
    case 12:
      return finishDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 13:
      return finishDatetime().isNull() || stopDatetime().isNull()
          ? QVariant() : QString::number(waitingMillis()/1000.0);
    case 14:
      return finishDatetime().isNull() || queueDatetime().isNull()
          ? QVariant() : QString::number(durationMillis()/1000.0);
    case 15:
      return queueDatetime().toString(
          QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 16:
      return queueDatetime().isNull() || creationDatetime().isNull()
                 ? QVariant() : QString::number(plannedMillis()/1000.0);
    case 17:
      return _queuewhen.toString();
    case 18:
      return _cancelwhen.toString();
    }
    break;
  default:
    ;
  }
  return QVariant();
}

TaskInstanceData *TaskInstance::data() {
  return detachedData<TaskInstanceData>();
}
