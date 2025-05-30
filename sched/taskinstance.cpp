/* Copyright 2012-2025 Hallowyn and others.
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
#include "condition/disjunctioncondition.h"
#include "modelview/templatedshareduiitemdata.h"
#include "util/paramsprovidermerger.h"
#include "format/timeformats.h"

static QAtomicInt _sequence;

class TaskInstanceData
    : public SharedUiItemDataWithMutableParams<TaskInstanceData,true> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  quint64 _id, _herdid, _parentid;
  Utf8String _idAsString, _taskId, _cause;
  mutable AtomicValue<Task> _task;
  QDateTime _creationDateTime;
  bool _force;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _queue, _start, _stop, _finish;
  mutable bool _success, _had_stderr;
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
  mutable AtomicValue<Host> _target;
  mutable bool _abortable;
  mutable AtomicValue<QList<QPair<Utf8String,quint64>>> _herdedTasksIdsPairs;
  mutable AtomicValue<QList<quint64>> _children;
  mutable AtomicValue<int> _remainingTries;
  Condition _queuewhen, _cancelwhen;

  TaskInstanceData(Task task, ParamSet params, bool force,
                   quint64 herdid, Condition queuewhen, Condition cancelwhen,
                   quint64 parentid, const Utf8String &cause)
    : SharedUiItemDataWithMutableParams<TaskInstanceData,true>(
        params.withParent(task.params())),
      _id(newId()), _herdid(herdid == 0 ? _id : herdid), _parentid(parentid),
      _idAsString(Utf8String::number(_id)),
      _taskId(task.id()), _cause(cause), _task(task),
      _creationDateTime(QDateTime::currentDateTime()), _force(force),
      _queue(LLONG_MIN), _start(LLONG_MIN), _stop(LLONG_MIN),
      _finish(LLONG_MIN),
      _success(false), _had_stderr(false), _returnCode(0), _abortable(false),
      _remainingTries(task.maxTries()),
      _queuewhen(queuewhen), _cancelwhen(cancelwhen) {}
  TaskInstanceData()
    : _queue(LLONG_MIN), _start(LLONG_MIN), _stop(LLONG_MIN),
      _finish(LLONG_MIN) {}

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
  Utf8String id() const override { return _idAsString; }
  QVariant uiData(int section, int role) const override;
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
    auto creation = _creationDateTime.toMSecsSinceEpoch();
    if (_queue != LLONG_MIN) // queued or further
      return _queue - creation;
    if (_finish != LLONG_MIN) // canceled before being queued
      return _finish - creation;
    // still planned : taking live value so far
    return QDateTime::currentMSecsSinceEpoch() - creation;
  }
  qint64 inline queuedMillis() const {
    if (_queue == LLONG_MIN) // still planned
      return 0;
    if (_start != LLONG_MIN) // running, waiting or finished
      return _start - _queue;
    if (_finish != LLONG_MIN) // canceled
      return _finish - _queue;
    // still queued : taking live value so far
    return QDateTime::currentMSecsSinceEpoch() - _queue;
  }
  qint64 inline runningMillis() const {
    if (_start == LLONG_MIN) // not started
      return 0;
    if (_stop == LLONG_MIN) // not finished : taking live value so far
      return QDateTime::currentMSecsSinceEpoch() - _start;
    return _stop - _start;
  }
  qint64 inline waitingMillis() const {
    if (_stop == LLONG_MIN) // still running (or before, or canceled)
      return 0;
    if (_finish == LLONG_MIN) // not finished : taking live value so far
      return QDateTime::currentMSecsSinceEpoch() - _start;
    return _finish - _stop;
  }
  qint64 inline durationMillis() const {
    if (_start == LLONG_MIN) // still planned, queued or canceled
      return 0;
    if (_finish == LLONG_MIN) // not finished : taking live value so far
      return QDateTime::currentMSecsSinceEpoch() - _start;
    return _finish - _start;
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
    Task task, bool force, ParamSet params, quint64 herdid,
    Condition queuewhen, Condition cancelwhen,
    quint64 parentid, const Utf8String &cause)
  : SharedUiItem(new TaskInstanceData(
                   task, params, force, herdid, queuewhen, cancelwhen,
                   parentid, cause)) {
}

Task TaskInstance::task() const {
  const TaskInstanceData *d = data();
  if (!d)
    return {};
  return d->_task.data();
}

Utf8String TaskInstance::taskId() const {
  const TaskInstanceData *d = data();
  if (!d)
    return {};
  return d->_taskId;
}

void TaskInstance::setParam(
    const Utf8String &key, const TypedValue &value) const {
  auto d = data();
  if (!d)
    return;
  auto params = d->_params.lockedData();
  params->insert(key, value);
}

void TaskInstance::paramAppend(const Utf8String &key, const TypedValue &value) const {
  auto d = data();
  if (!d)
    return;
  auto params = d->_params.lockedData();
  auto current = params->paramRawValue(key);
  if (!current)
    params->insert(key, value);
  else
    params->insert(key, Utf8String(current)+" "_u8+Utf8String(value));
}

ParamSet TaskInstance::params() const {
  const TaskInstanceData *d = data();
  return d ? d->_params.detachedData() : ParamSet();
}

quint64 TaskInstance::idAsLong() const {
  const TaskInstanceData *d = data();
  return d ? d->_id : 0;
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

bool TaskInstance::had_stderr() const {
  const TaskInstanceData *d = data();
  return d ? d->_had_stderr : false;
}

void TaskInstance::set_had_stderr(bool had_stderr) const {
  const TaskInstanceData *d = data();
  if (d)
    d->_had_stderr = had_stderr;
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
  return d ? d->_target.data() : Host();
}

void TaskInstance::setTarget(Host target) const {
  const TaskInstanceData *d = data();
  if (d) {
    d->_target = target;
  }
}

QMap<QString,QString> TaskInstance::varsAsEnv() const {
  QMap<QString,QString> env;
  auto vars = task().vars();
  for (const auto &key: vars.paramKeys()) {
    if (key.isEmpty()) [[unlikely]]
      continue;
    auto value = PercentEvaluator::eval_utf8(vars.paramRawUtf8(key), this);
    env.insert(key.toIdentifier(), value);
  }
  return env;
}

QMap<QString,QString> TaskInstance::varsAsHeaders() const {
  QMap<QString,QString> env;
  auto vars = task().vars();
  for (const auto &key: vars.paramKeys()) {
    if (key.isEmpty()) [[unlikely]]
      continue;
    auto value = PercentEvaluator::eval_utf8(vars.paramRawUtf8(key), this);
    value.remove('\r').replace('\n', ' ');
    env.insert(key.toInternetHeaderName(), value);
  }
  return env;
}

void TaskInstance::setTask(Task task) const {
  const TaskInstanceData *d = data();
  if (d) {
    auto p = d->_params.lockedData();
    d->_task = task;
    p->setParent(task.params());
    d->_remainingTries = task.maxTries();
  }
}

bool TaskInstance::force() const {
  const TaskInstanceData *d = data();
  return d ? d->_force : false;
}

quint64 TaskInstance::herdid() const {
  const TaskInstanceData *d = data();
  return d ? d->_herdid : 0;
}

void TaskInstance::appendToHerd(const Utf8String &taskid, quint64 tii) const {
  const TaskInstanceData *d = data();
  if (!d)
    return;
  d->_herdedTasksIdsPairs.lockedData()->append({taskid, tii});
}

QList<QPair<Utf8String,quint64>> TaskInstance::herdedTasksIdsPairs() const {
  const TaskInstanceData *d = data();
  return d ? d->_herdedTasksIdsPairs.detachedData()
           : QList<QPair<Utf8String,quint64>>{};
}

void TaskInstance::appendToChildren(quint64 tii) const {
  const TaskInstanceData *d = data();
  if (!d)
    return;
  d->_children.lockedData()->append(tii);
}

QList<quint64> TaskInstance::children() const {
  const TaskInstanceData *d = data();
  return d ? d->_children.detachedData() : QList<quint64>{};
}

quint64 TaskInstance::parentid() const {
  const TaskInstanceData *d = data();
  return d ? d->_parentid : 0;
}

Utf8String TaskInstance::cause() const {
  const TaskInstanceData *d = data();
  return d ? d->_cause : Utf8String{};
}

void TaskInstance::consumeOneTry() const {
  const TaskInstanceData *d = data();
  if (!d)
    return;
  auto r = d->_remainingTries.lockedData();
  *r = std::max(*r-1, 0);
}

void TaskInstance::consumeAllTries() const {
  const TaskInstanceData *d = data();
  if (!d)
    return;
  d->_remainingTries.setData(0);
}

int TaskInstance::remainingTries() const {
  const TaskInstanceData *d = data();
  return d ? d->_remainingTries.data() : 0;
}

int TaskInstance::currentTry() const {
  const TaskInstanceData *d = data();
  // FIXME not sure it works well if task.maxTries change meanwhile
  return d ? d->_task.lockedData()->maxTries() - d->_remainingTries.data() : 0;
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

QVariant TaskInstanceData::uiData(int section, int role) const {
  switch(role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case SharedUiItem::ExternalDataRole:
      switch(section) {
        case 0:
          return _idAsString;
        case 1:
          return _taskId;
        case 2:
          return TaskInstance::statusAsString(status());
        case 3:
          return creationDatetime().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
        case 4:
          return startDatetime().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
        case 5:
          return stopDatetime().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
        case 6:
          return startDatetime().isNull() || queueDatetime().isNull()
              ? QVariant() : QString::number(queuedMillis()/1000.0);
        case 7:
          return stopDatetime().isNull() || startDatetime().isNull()
              ? QVariant() : QString::number(runningMillis()/1000.0);
        case 8:
          return {}; // custom actions, handled by the model, if needed
        case 9:
          return _abortable;
        case 10:
          return _herdid;
        case 11: {
          auto ids = _herdedTasksIdsPairs.lockedData();
          QString s;
          for (const QPair<Utf8String,quint64> &id: *ids)
            s += id.first + "/" + QString::number(id.second) + " ";
          s.chop(1);
          return s;
        }
        case 12:
          return finishDatetime().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
        case 13:
          return finishDatetime().isNull() || stopDatetime().isNull()
              ? QVariant() : QString::number(waitingMillis()/1000.0);
        case 14:
          return finishDatetime().isNull() || queueDatetime().isNull()
              ? QVariant() : QString::number(durationMillis()/1000.0);
        case 15:
          return queueDatetime().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
        case 16:
          return queueDatetime().isNull() || creationDatetime().isNull()
              ? QVariant() : QString::number(plannedMillis()/1000.0);
        case 17:
          return _queuewhen.toString();
        case 18:
          return _cancelwhen.toString();
        case 19:
          return PercentEvaluator::eval(
                _task.lockedData()->deduplicateCriterion(), this).as_qvariant();
        case 20:
          return _parentid;
        case 21:
          return _cause;
        case 22:
          return _had_stderr;
      }
      break;
    default:
      ;
  }
  return {};
}

// TaskInstanceData *TaskInstance::data() { // should never detach/deep copy
//   return detachedData<TaskInstanceData>();
// }

const TaskInstanceData *TaskInstance::data() const {
  return specializedData<TaskInstanceData>();
}

const SharedUiItemDataFunctions TaskInstanceData::_paramFunctions {
  { "!taskinstanceid", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)->_id;
    } },
  { "!herdid", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)->_herdid;
    } },
  { "!parenttaskinstanceid", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)->_parentid;
    } },
  { "!childrenids", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto children = reinterpret_cast<const TaskInstanceData*>(data)
                      ->_children.lockedData();
      Utf8StringList list;
      for (const auto &tii: *children)
        list.append(Utf8String::number(tii));
      children.unlock();
      return list.join(' ');
    } },
  { "!taskinstancecause", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)->_cause;
    } },
  { "!runningms", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)->runningMillis();
    } },
  { "!runnings", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->runningMillis()/1e3;
    } },
  { "!waitingms", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->waitingMillis();
    } },
  { "!waitings", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->waitingMillis()/1e3;
    } },
  { "!plannedms", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->plannedMillis();
    } },
  { "!planneds", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->plannedMillis()/1e3;
    } },
  { "!queuedms", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->queuedMillis();
    } },
  { "!queueds", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->queuedMillis()/1e3;
    } },
  // total[m]s: backward compatiblity with qron < 1.12
  { { "!durationms", "!totalms" }, [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->durationMillis();
    } },
  { { "!durations", "!totals" }, [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->durationMillis()/1e3;
    } },
  { "!returncode", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->_returnCode;
    } },
  { "!status", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return TaskInstance::statusAsString(
            reinterpret_cast<const TaskInstanceData*>(data)->status());
    } },
  { "!target", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto target = reinterpret_cast<const TaskInstanceData*>(data)
                  ->_target.lockedData();
      return target->id();
    } },
  { "!configuredtarget", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto tid = reinterpret_cast<const TaskInstanceData*>(data);
      return tid->_task.lockedData()->target();
    } },
  { "!targethostname", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto target = reinterpret_cast<const TaskInstanceData*>(data)
                  ->_target.lockedData();
      return target->hostname();
    } },
  { "!requestdate", [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int ml) STATIC_LAMBDA -> TypedValue {
      return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
            reinterpret_cast<const TaskInstanceData*>(data)->creationDatetime(),
            key.mid(ml), context);
    }, true },
  { "!startdate", [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int ml) STATIC_LAMBDA -> TypedValue {
      return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
            reinterpret_cast<const TaskInstanceData*>(data)->startDatetime(),
            key.mid(ml), context);
    }, true },
  { "!stopdate", [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int ml) STATIC_LAMBDA -> TypedValue {
      return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
            reinterpret_cast<const TaskInstanceData*>(data)->stopDatetime(),
            key.mid(ml), context);
    }, true },
  { "!finishdate", [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int ml) STATIC_LAMBDA -> TypedValue {
      return TimeFormats::toMultifieldSpecifiedCustomTimestamp(
            reinterpret_cast<const TaskInstanceData*>(data)->finishDatetime(),
            key.mid(ml), context);
    }, true },
  { "!remainingtries", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->_remainingTries.data();
    } },
  { "!currenttry", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto tid = reinterpret_cast<const TaskInstanceData*>(data);
       // FIXME not sure it works well if task.maxTries change meanwhile
      return tid->_task.lockedData()->maxTries() - tid->_remainingTries.data();
    } },
  { "!deduplicatecriterion", [](const SharedUiItemData *data, const Utf8String&,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto tid = reinterpret_cast<const TaskInstanceData*>(data);
      return PercentEvaluator::eval_utf8(
            tid->_task.lockedData()->deduplicateCriterion(), tid);
    } },
  { "!hadstderr", [](const SharedUiItemData *data, const Utf8String &,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      return reinterpret_cast<const TaskInstanceData*>(data)
          ->_had_stderr;
    } },
#if 0
  { "!varsasenv", [](const SharedUiItemData *data, const Utf8String&,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto tid = (TaskInstanceData*)(data);
      auto map = TaskInstance(tid).varsAsEnv();
      qDebug() << "**** !varsasenv" << map;
      Utf8StringList s;
      for (const auto &key: map.keys())
        s += key+"="+map.value(key);
      return s.join(' ');
    } },
  { "!varsasheaders", [](const SharedUiItemData *data, const Utf8String&,
        const PercentEvaluator::EvalContext&, int) STATIC_LAMBDA -> TypedValue {
      auto tid = (TaskInstanceData*)(data);
      auto map = TaskInstance(tid).varsAsHeaders();
      qDebug() << "**** !varsasheaders" << map;
      Utf8StringList s;
      for (const auto &key: map.keys())
        s += key+"="+map.value(key);
      return s.join(' ');
    } },
#endif
  // this is needed b/c otherwise the "!" prefix passthrough below would hide
  // params starting with a !
  { { "!parenttaskid", "!parenttasklocalid" },
    [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int) STATIC_LAMBDA -> TypedValue {
      auto tid = reinterpret_cast<const TaskInstanceData*>(data);
      return tid->_params.lockedData()->paramRawValue(key, context);
    } },
  { "!", [](const SharedUiItemData *data, const Utf8String &key,
        const PercentEvaluator::EvalContext &context, int) STATIC_LAMBDA -> TypedValue {
      auto tid = reinterpret_cast<const TaskInstanceData*>(data);
      PercentEvaluator::EvalContext new_context = context;
      new_context.setScopeFilter({}); // in case it was [taskinstance]
      return tid->_task.lockedData()->paramRawValue(key, new_context);
    }, true },
};

#if 0
TaskInstance::TaskInstance(TaskInstanceData *data) : SharedUiItem(data) {}
#endif

const Utf8String TaskInstanceData::_qualifier = "taskinstance"_u8;

const Utf8StringIndexedConstList TaskInstanceData::_sectionNames {
  "taskinstanceid", // 0
  "taskid",
  "status",
  "creation_date",
  "start_date",
  "stop_date", // 5
  "time_queued",
  "time_running",
  "actions",
  "abortable",
  "herdid", // 10
  "herded_task_instances",
  "finish_date",
  "time_waiting",
  "duration",
  "queue_date", // 15
  "time_planned",
  "queue_when",
  "cancel_when",
  "deduplicate_criterion",
  "parentid", // 20
  "cause",
  "had_stderr",
};

const Utf8StringIndexedConstList TaskInstanceData::_headerNames {
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
  "Cancel when",
  "Deduplicate criterion",
  "Parent Id", // 20
  "Cause",
  "Had Stderr",
};
