/* Copyright 2012-2013 Hallowyn and others.
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
#include "taskrequest.h"
#include <QSharedData>
#include <QDateTime>
#include <QAtomicInt>

static QAtomicInt _sequence;

class TaskRequestData : public QSharedData {
public:
  quint64 _id;
  Task _task;
  ParamSet _params;
  QDateTime _submission;
  bool _force;
  QString _command;
  ParamSet _setenv;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _start, _end;
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

  TaskRequestData(Task task, ParamSet params, bool force)
    : _id(newId()), _task(task), _params(params),
      _submission(QDateTime::currentDateTime()), _force(force),
      _command(task.command()), _setenv(task.setenv()),
      _start(LLONG_MIN), _end(LLONG_MIN),
      _success(false), _returnCode(0), _abortable(false) { }
  TaskRequestData() : _id(0), _force(false),
      _start(LLONG_MIN), _end(LLONG_MIN), _success(false), _returnCode(0),
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
};

TaskRequest::TaskRequest() {
}

TaskRequest::TaskRequest(const TaskRequest &other)
  : ParamsProvider(), d(other.d) {
}

TaskRequest::TaskRequest(Task task, bool force)
  : d(new TaskRequestData(task, task.params().createChild(), force)) {
}

TaskRequest::~TaskRequest() {
}

TaskRequest &TaskRequest::operator=(const TaskRequest &other) {
  if (this != &other)
    d.operator=(other.d);
  return *this;
}

bool TaskRequest::operator==(const TaskRequest &other) const {
  return (!d && !other.d) || (d && other.d && d->_id == other.d->_id);
}

Task TaskRequest::task() const {
  return d ? d->_task : Task();
}

ParamSet TaskRequest::params() const {
  return d ? d->_params : ParamSet();
}

void TaskRequest::overrideParam(QString key, QString value) {
  if (d)
    d->_params.setValue(key, value);
}

quint64 TaskRequest::id() const {
  return d ? d->_id : 0;
}

QDateTime TaskRequest::submissionDatetime() const {
  return d ? d->_submission : QDateTime();
}


QDateTime TaskRequest::startDatetime() const {
  return d && d->_start != LLONG_MIN
      ? QDateTime::fromMSecsSinceEpoch(d->_start) : QDateTime();
}

void TaskRequest::setStartDatetime(QDateTime datetime) const {
  if (d)
    d->_start = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN;
}

QDateTime TaskRequest::endDatetime() const {
  return d && d->_end != LLONG_MIN
      ? QDateTime::fromMSecsSinceEpoch(d->_end) : QDateTime();
}

void TaskRequest::setEndDatetime(QDateTime datetime) const {
  if (d)
    d->_end = datetime.isValid() ? datetime.toMSecsSinceEpoch() : LLONG_MIN;
}

bool TaskRequest::success() const {
  return d ? d->_success : false;
}

void TaskRequest::setSuccess(bool success) const {
  if (d)
    d->_success = success;
}

int TaskRequest::returnCode() const {
  return d ? d->_returnCode : -1;
}

void TaskRequest::setReturnCode(int returnCode) const {
  if (d)
    d->_returnCode = returnCode;
}

Host TaskRequest::target() const {
  return d ? d->_target : Host();
}

void TaskRequest::setTarget(Host target) const {
  if (d) {
    target.detach();
    d->_target = target;
  }
}

QVariant TaskRequest::paramValue(QString key, QVariant defaultValue) const {
  //Log::fatal() << "TaskRequest::paramvalue " << key;
  if (!d)
    return defaultValue;
  if (key == "!taskid") {
    return task().id();
  } else if (key == "!fqtn") {
    return task().fqtn();
  } else if (key == "!taskgroupid") {
    return task().taskGroup().id();
  } else if (key == "!taskrequestid") {
    return QString::number(id());
  } else if (key == "!runningms") {
    return QString::number(runningMillis());
  } else if (key == "!runnings") {
    return QString::number(runningMillis()/1000);
  } else if (key == "!queuedms") {
    return QString::number(queuedMillis());
  } else if (key == "!queueds") {
    return QString::number(queuedMillis()/1000);
  } else if (key == "!totalms") {
    return QString::number(queuedMillis()+runningMillis());
  } else if (key == "!totals") {
    return QString::number((queuedMillis()+runningMillis())/1000);
  } else if (key == "!returncode") {
    return QString::number(returnCode());
  } else if (key == "!status") {
    if (startDatetime().isNull())
      return "queued";
    if (endDatetime().isNull())
      return "running";
    return success() ? "success" : "failure";
  } else if (key == "!submissiondate") {
    return submissionDatetime().toString("yyyy-MM-dd hh:mm:ss,zzz");
  } else if (key == "!startdate") {
    return startDatetime().toString("yyyy-MM-dd hh:mm:ss,zzz");
  } else if (key == "!enddate") {
    return endDatetime().toString("yyyy-MM-dd hh:mm:ss,zzz");
  } else if (key == "!target") {
    return target().hostname();
  }
  return defaultValue;
}

ParamSet TaskRequest::setenv() const {
  return d ? d->_setenv : ParamSet();
}

void TaskRequest::setTask(Task task) {
  if (d)
    d->_task = task;
}

bool TaskRequest::force() const {
  return d ? d->_force : false;
}

TaskRequest::TaskRequestStatus TaskRequest::status() const {
  if (d) {
    if (d->_end != LLONG_MIN) {
      if (d->_start == LLONG_MIN)
        return Canceled;
      else
        return d->_success ? Success : Failure;
    }
    if (d->_start != LLONG_MIN)
      return Running;
  }
  return Queued;
}

QString TaskRequest::statusAsString(
    TaskRequest::TaskRequestStatus status) {
  switch(status) {
  case Queued:
    return "queued";
  case Running:
    return "running";
  case Success:
    return "success";
  case Failure:
    return "failure";
  case Canceled:
    return "canceled";
  }
  return "unknown";
}

bool TaskRequest::isNull() {
  return !d || d->_id == 0;
}

uint qHash(const TaskRequest &request) {
  return (uint)request.id();
}

QString TaskRequest::command() const {
  return d ? d->_command : QString();
}

void TaskRequest::overrideCommand(QString command) {
  if (d)
    d->_command = command;
}

void TaskRequest::overrideSetenv(QString key, QString value) {
  if (d)
    d->_setenv.setValue(key, value);
}

bool TaskRequest::abortable() const {
  return d && d->_abortable;
}

void TaskRequest::setAbortable(bool abortable) const {
  if (d)
    d->_abortable = abortable;
}
