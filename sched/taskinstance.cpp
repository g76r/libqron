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
#include "taskinstance.h"
#include <QSharedData>
#include <QDateTime>
#include <QAtomicInt>
#include "format/timeformats.h"
#include <functional>
#include "util/radixtree.h"
#include "thread/atomicvalue.h"

static QString _uiHeaderNames[] = {
  "Instance Id", // 0
  "Task Id",
  "Status",
  "Request Date",
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
  "Total time", // 14
};

static QAtomicInt _sequence;

class TaskInstanceData : public SharedUiItemData {
public:
  quint64 _id, _groupId;
  QString _idAsString;
  Task _task;
  ParamSet _overridingParams;
  QDateTime _requestDateTime;
  bool _force;
  TaskInstance _herder;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _start, _stop, _finish;
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

  TaskInstanceData(Task task, ParamSet overridingParams, bool force,
                   TaskInstance herder = TaskInstance(), quint64 groupId = 0)
    : _id(newId()), _groupId(groupId ? groupId : _id),
      _idAsString(QString::number(_id)),
      _task(task), _overridingParams(overridingParams),
      _requestDateTime(QDateTime::currentDateTime()), _force(force),
      _herder(herder), _start(LLONG_MIN), _stop(LLONG_MIN), _finish(LLONG_MIN),
      _success(false), _returnCode(0), _abortable(false) {
    _overridingParams.setParent(task.params());
  }
  TaskInstanceData() : _id(0), _groupId(0), _force(false),
      _start(LLONG_MIN), _stop(LLONG_MIN), _finish(LLONG_MIN),
      _success(false), _returnCode(0), _abortable(false) { }

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
  QDateTime inline requestDatetime() const { return _requestDateTime; }
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
  qint64 inline queuedMillis() const { return _requestDateTime.msecsTo(startDatetime()); }
  qint64 inline runningMillis() const {
    return _start != LLONG_MIN && _stop != LLONG_MIN ? _stop - _start : 0; }
  qint64 inline waitingMillis() const {
    return _stop != LLONG_MIN && _finish != LLONG_MIN ? _finish - _stop : 0; }
  qint64 inline totalMillis() const {
    return _requestDateTime.isValid() && _finish != LLONG_MIN
        ? _finish - _requestDateTime.toMSecsSinceEpoch() : 0; }
  qint64 inline liveTotalMillis() const {
    return (_finish != LLONG_MIN ? _finish
                                 : QDateTime::currentMSecsSinceEpoch())
        - _requestDateTime.toMSecsSinceEpoch(); }
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
    return TaskInstance::Queued;
  }
};

TaskInstance::TaskInstance() {
}

TaskInstance::TaskInstance(const TaskInstance &other) : SharedUiItem(other) {
}

TaskInstance::TaskInstance(
    Task task, bool force, ParamSet params, TaskInstance herder)
  : SharedUiItem(new TaskInstanceData(task, params, force, herder)) {
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

void TaskInstance::setParam(QString key, QString value) {
  TaskInstanceData *d = data();
  if (d)
    d->_overridingParams.setValue(key, value);
}

ParamSet TaskInstance::params() const {
  const TaskInstanceData *d = data();
  return d ? d->_overridingParams : ParamSet();
}

quint64 TaskInstance::idAsLong() const {
  const TaskInstanceData *d = data();
  return d ? d->_id : 0;
}

quint64 TaskInstance::groupId() const {
  const TaskInstanceData *d = data();
  return d ? d->_groupId : 0;
}

QDateTime TaskInstance::requestDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->requestDatetime() : QDateTime();
}

QDateTime TaskInstance::startDatetime() const {
  const TaskInstanceData *d = data();
  return d ? d->requestDatetime() : QDateTime();
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

qint64 TaskInstance::totalMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->totalMillis() : 0;
}

qint64 TaskInstance::liveTotalMillis() const {
  const TaskInstanceData *d = data();
  return d ? d->liveTotalMillis() : 0;
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
  if (sheeps->isEmpty())
    return; // keep own status
  switch(herdingpolicy) {
  case Task::WaitAnd:
    if (!d->_success)
      return;
    for (auto sheep: *sheeps)
      if (!sheep.success()) {
        d->_success = false;
        return;
      }
    d->_success = true;
    return;
  case Task::WaitOr:
    for (auto sheep: *sheeps)
      if (sheep.success()) {
        d->_success = true;
        return;
      }
    d->_success = false;
    return;
  case Task::WaitOwn:
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
{ "!totalms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.runningMillis()+taskInstance.queuedMillis();
} },
{ "!herdtotalms", [](const TaskInstance &taskInstance, const QString&) {
  return taskInstance.herder().runningMillis()+taskInstance.queuedMillis();
} },
{ "!totals", [](const TaskInstance &taskInstance, const QString&) {
  return (taskInstance.runningMillis()+taskInstance.queuedMillis())/1000;
} },
{ "!herdtotals", [](const TaskInstance &taskInstance, const QString&) {
  return (taskInstance.herder().runningMillis()+taskInstance.queuedMillis())
      /1000;
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
        taskInstance.requestDatetime(), key.mid(15));
}, true },
{ "!herdrequestdate", [](const TaskInstance &taskInstance, const QString &key) {
  return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
        taskInstance.herder().requestDatetime(), key.mid(19));
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
  sheeps->append(sheep);
}

static QHash<TaskInstance::TaskInstanceStatus,QString> _statuses {
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
      return requestDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 4:
      return startDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 5:
      return stopDatetime().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 6:
      return startDatetime().isNull() || requestDatetime().isNull()
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
      return finishDatetime().isNull() || requestDatetime().isNull()
          ? QVariant() : QString::number(totalMillis()/1000.0);
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
