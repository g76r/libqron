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
#include "task.h"
#include "taskgroup.h"
#include <QString>
#include <QMap>
#include "pf/pfnode.h"
#include "crontrigger.h"
#include "log/log.h"
#include <QAtomicInt>
#include "event/event.h"
#include <QWeakPointer>
#include "sched/scheduler.h"
#include <QtDebug>
#include <QThread>

class TaskData : public QSharedData {
public:
  QString _id, _label, _mean, _command, _target;
  TaskGroup _group;
  ParamSet _params;
  QSet<QString> _noticeTriggers;
  QMap<QString,qint64> _resources;
  quint32 _maxInstances;
  QList<CronTrigger> _cronTriggers;
  QList<QRegExp> _stderrFilters;
  QList<Event> _onstart, _onsuccess, _onfailure;
  QWeakPointer<Scheduler> _scheduler;

private:
  mutable qint64 _lastExecution, _nextScheduledExecution;
  mutable QAtomicInt _instancesCount;

public:
  TaskData() : _lastExecution(LLONG_MIN), _nextScheduledExecution(LLONG_MIN) { }
  TaskData(const TaskData &other) : QSharedData(), _id(other._id),
    _label(other._label), _mean(other._mean), _command(other._command),
    _target(other._target), _group(other._group), _params(other._params),
    _noticeTriggers(other._noticeTriggers), _resources(other._resources),
    _maxInstances(other._maxInstances), _cronTriggers(other._cronTriggers),
    _stderrFilters(other._stderrFilters), _lastExecution(other._lastExecution),
    _nextScheduledExecution(other._nextScheduledExecution) { }
  QDateTime lastExecution() const {
    return _lastExecution == LLONG_MIN
        ? QDateTime() : QDateTime::fromMSecsSinceEpoch(_lastExecution); }
  void setLastExecution(QDateTime timestamp) const {
    _lastExecution = timestamp.toMSecsSinceEpoch(); }
  QDateTime nextScheduledExecution() const {
    return _nextScheduledExecution == LLONG_MIN
        ? QDateTime()
        : QDateTime::fromMSecsSinceEpoch(_nextScheduledExecution); }
  void setNextScheduledExecution(QDateTime timestamp) const {
    _nextScheduledExecution = timestamp.toMSecsSinceEpoch(); }
  int instancesCount() const { return _instancesCount; }
  int fetchAndAddInstancesCount(int valueToAdd) const {
    return _instancesCount.fetchAndAddOrdered(valueToAdd); }
  ~TaskData() {
    qDebug() << "~TaskData" << QThread::currentThread()->objectName()
             << _id;
  }};

Task::Task() {
}

Task::Task(const Task &other) : d(other.d) {
}

Task::Task(PfNode node, Scheduler *scheduler) {
  TaskData *td = new TaskData;
  td->_scheduler = scheduler;
  td->_id = node.attribute("id"); // LATER check uniqueness
  td->_label = node.attribute("label", td->_id);
  td->_mean = node.attribute("mean"); // LATER check validity
  td->_command = node.attribute("command");
  td->_target = node.attribute("target");
  if (td->_target.isEmpty()
      && (td->_mean == "local" || td->_mean == "donothing"))
    td->_target = "localhost";
  td->_maxInstances = node.attribute("maxinstances", "1").toInt();
  if (td->_maxInstances <= 0) {
    td->_maxInstances = 1;
    Log::error() << "ignoring invalid task maxinstances " << node.toPf();
  }
  foreach (PfNode child, node.childrenByName("param")) {
    QString key = child.attribute("key");
    QString value = child.attribute("value");
    if (key.isNull() || value.isNull()) {
      Log::error() << "ignoring invalid task param " << child.toPf();
    } else {
      //Log::debug() << "configured task param " << key << "=" << value
      //             << "for task '" << td->_id << "'";
      td->_params.setValue(key, value);
    }
  }
  foreach (PfNode child, node.childrenByName("trigger")) {
    foreach (PfNode grandchild, child.children()) {
      QString content = grandchild.contentAsString();
      QString triggerType = grandchild.name();
      if (triggerType == "notice") {
        if (!content.isEmpty()) {
          td->_noticeTriggers.insert(content);
          //Log::debug() << "configured notice trigger '" << content
          //             << "' on task '" << td->_id << "'";
        } else
          Log::error() << "ignoring empty notice trigger on task '" << td->_id
                       << "' in configuration";
      } else if (triggerType == "cron") {
          CronTrigger trigger(content);
          if (trigger.isValid()) {
            td->_cronTriggers.append(trigger);
            //Log::debug() << "configured cron trigger '" << content
            //             << "' on task '" << td->_id << "'";
          } else
            Log::error() << "ignoring invalid cron trigger '" << content
                         << "' parsed as '" << trigger.canonicalCronExpression()
                         << "' on task '" << td->_id;
          // LATER read misfire config
      } else {
        Log::error() << "ignoring unknown trigger type '" << triggerType
                     << "' on task '" << td->_id << "'";
      }
    }
  }
  foreach (PfNode child, node.childrenByName("onstart"))
    scheduler->loadEventListConfiguration(child, td->_onstart);
  foreach (PfNode child, node.childrenByName("onsuccess"))
    scheduler->loadEventListConfiguration(child, td->_onsuccess);
  foreach (PfNode child, node.childrenByName("onfailure"))
    scheduler->loadEventListConfiguration(child, td->_onfailure);
  foreach (PfNode child, node.childrenByName("onfinish")) {
    scheduler->loadEventListConfiguration(child, td->_onsuccess);
    scheduler->loadEventListConfiguration(child, td->_onfailure);
  }
  foreach (PfNode child, node.childrenByName("resource")) {
    QString kind = child.attribute("kind");
    qint64 quantity = child.attribute("quantity").toLong(0, 0);
    if (kind.isNull())
      Log::error() << "ignoring resource with no or empty kind in task"
                   << td->_id;
    else if (quantity <= 0)
      Log::error() << "ignoring resource of kind " << kind
                   << "with incorrect quantity in task" << td->_id;
    else
      td->_resources.insert(kind, quantity);
  }
  d = td;
}

Task::~Task() {
  //qDebug() << "~Task" << QThread::currentThread()->objectName();
}

Task &Task::operator =(const Task &other) {
  if (this != &other)
    d.operator=(other.d);
  return *this;
}

ParamSet Task::params() const {
  return d ? d->_params : ParamSet();
}

bool Task::isNull() const {
  return !d;
}

QSet<QString> Task::noticeTriggers() const {
  return d ? d->_noticeTriggers : QSet<QString>();
}

QString Task::id() const {
  return d ? d->_id : QString();
}

QString Task::fqtn() const {
  return d ? d->_group.id()+"."+d->_id : QString();
}

QString Task::label() const {
  return d ? d->_label : QString();
}

QString Task::mean() const {
  return d ? d->_mean : QString();
}

QString Task::command() const {
  return d ? d->_command : QString();
}

QString Task::target() const {
  return d ? d->_target : QString();
}

TaskGroup Task::taskGroup() const {
  return d ? d->_group : TaskGroup();
}

void Task::completeConfiguration(TaskGroup taskGroup) {
  if (!d)
    return;
  d->_group = taskGroup;
  d->_params.setParent(taskGroup.params());
  QString filter = params().value("stderrfilter");
  if (!filter.isEmpty())
    d->_stderrFilters.append(QRegExp(filter));
}

QList<CronTrigger> Task::cronTriggers() const {
  return d ? d->_cronTriggers : QList<CronTrigger>();
}

QMap<QString, qint64> Task::resources() const {
  return d ? d->_resources : QMap<QString,qint64>();
}

QString Task::resourcesAsString() const {
  QString s;
  s.append("{ ");
  if (d)
    foreach(QString key, d->_resources.keys()) {
      s.append(key).append("=")
          .append(QString::number(d->_resources.value(key))).append(" ");
    }
  return s.append("}");
}

QString Task::triggersAsString() const {
  QString s;
  if (d) {
    foreach (CronTrigger t, d->_cronTriggers)
      s.append("(").append(t.cronExpression()).append(") ");
    foreach (QString t, d->_noticeTriggers)
      s.append("^").append(t).append(" ");
  }
  if (!s.isEmpty())
    s.chop(1); // remove last space
  return s;
}

QDateTime Task::lastExecution() const {
  return d ? d->lastExecution() : QDateTime();
}

QDateTime Task::nextScheduledExecution() const {
  return d ? d->nextScheduledExecution() : QDateTime();
}

void Task::setLastExecution(const QDateTime timestamp) const {
  if (d)
    d->setLastExecution(timestamp);
}

void Task::setNextScheduledExecution(const QDateTime timestamp) const {
  if (d)
    d->setNextScheduledExecution(timestamp);
}

int Task::maxInstances() const {
  return d ? d->_maxInstances : 0;
}

int Task::instancesCount() const {
  return d ? d->instancesCount() : 0;
}

int Task::fetchAndAddInstancesCount(int valueToAdd) const {
  return d ? d->fetchAndAddInstancesCount(valueToAdd) : 0;
}

const QList<QRegExp> Task::stderrFilters() const {
  return d ? d->_stderrFilters : QList<QRegExp>();
}

void Task::appendStderrFilter(QRegExp filter) {
  if (d)
    d->_stderrFilters.append(filter);
}

QDebug operator<<(QDebug dbg, const Task &task) {
  dbg.nospace() << task.fqtn();
  return dbg.space();
}

void Task::triggerStartEvents(const ParamsProvider *context) const {
  if (d) {
    d->_group.triggerStartEvents(context);
    Scheduler::triggerEvents(d->_onstart, context);
  }
}

void Task::triggerSuccessEvents(const ParamsProvider *context) const {
  if (d) {
    d->_group.triggerSuccessEvents(context);
    Scheduler::triggerEvents(d->_onsuccess, context);
  }
}

void Task::triggerFailureEvents(const ParamsProvider *context) const {
  if (d) {
    d->_group.triggerFailureEvents(context);
    Scheduler::triggerEvents(d->_onfailure, context);
  }
}

const QList<Event> Task::onstartEvents() const {
  return d ? d->_onstart : QList<Event>();
}

const QList<Event> Task::onsuccessEvents() const {
  return d ? d->_onsuccess : QList<Event>();
}

const QList<Event> Task::onfailureEvents() const {
  return d ? d->_onfailure : QList<Event>();
}