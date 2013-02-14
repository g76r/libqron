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
#include "taskgroup.h"
#include "util/paramset.h"
#include <QString>
#include "task.h"
#include "pf/pfnode.h"
#include <QtDebug>
#include "event/event.h"
#include <QWeakPointer>
#include "sched/scheduler.h"

class TaskGroupData : public QSharedData {
public:
  QString _id, _label;
  ParamSet _params;
  QList<Event> _onstart, _onsuccess, _onfailure;
  QWeakPointer<Scheduler> _scheduler;
  TaskGroupData() { }
  TaskGroupData(const TaskGroupData &other) : QSharedData(), _id(other._id),
    _label(other._label), _params(other._params)/*, _tasks(other._tasks)*/ { }
  ~TaskGroupData() { }
};

TaskGroup::TaskGroup() : d(new TaskGroupData) {
}

TaskGroup::TaskGroup(const TaskGroup &other) : d(other.d) {
}

TaskGroup::TaskGroup(PfNode node, ParamSet parentParamSet,
                     Scheduler *scheduler) {
  TaskGroupData *tgd = new TaskGroupData;
  tgd->_scheduler = scheduler;
  tgd->_id = node.attribute("id"); // LATER check uniqueness
  tgd->_label = node.attribute("label", tgd->_id);
  tgd->_params.setParent(parentParamSet);
  foreach (PfNode child, node.childrenByName("param")) {
    QString key = child.attribute("key");
    QString value = child.attribute("value");
    if (key.isNull() || value.isNull()) {
      Log::warning() << "invalid taskgroup param " << child.toPf();
    } else {
      Log::debug() << "configured taskgroup param " << key << "=" << value
                   << "for taskgroup '" << tgd->_id << "'";
      tgd->_params.setValue(key, value);
    }
  }
  foreach (PfNode child, node.childrenByName("onstart"))
    scheduler->loadEventListConfiguration(child, tgd->_onstart);
  foreach (PfNode child, node.childrenByName("onsuccess"))
    scheduler->loadEventListConfiguration(child, tgd->_onsuccess);
  foreach (PfNode child, node.childrenByName("onfailure"))
    scheduler->loadEventListConfiguration(child, tgd->_onfailure);
  foreach (PfNode child, node.childrenByName("onfinish")) {
    scheduler->loadEventListConfiguration(child, tgd->_onsuccess);
    scheduler->loadEventListConfiguration(child, tgd->_onfailure);
  }
  d = tgd;
}

TaskGroup::~TaskGroup() {
}

TaskGroup &TaskGroup::operator =(const TaskGroup &other) {
  if (this != &other)
    d.operator=(other.d);
  return *this;
}

QString TaskGroup::id() const {
  return d->_id;
}

QString TaskGroup::label() const {
  return d->_label;
}

ParamSet TaskGroup::params() const {
  return d->_params;
}

bool TaskGroup::isNull() const {
  return d->_id.isNull();
}

QDebug operator<<(QDebug dbg, const TaskGroup &taskGroup) {
  dbg.nospace() << taskGroup.id();
  return dbg.space();
}

void TaskGroup::triggerStartEvents(const ParamsProvider *context) const {
  // LATER trigger events in parent group first
  Scheduler::triggerEvents(d->_onstart, context);
}

void TaskGroup::triggerSuccessEvents(const ParamsProvider *context) const {
  Scheduler::triggerEvents(d->_onsuccess, context);
}

void TaskGroup::triggerFailureEvents(const ParamsProvider *context) const {
  Scheduler::triggerEvents(d->_onfailure, context);
}

const QList<Event> TaskGroup::onstartEvents() const {
  return d->_onstart;
}

const QList<Event> TaskGroup::onsuccessEvents() const {
  return d->_onsuccess;
}

const QList<Event> TaskGroup::onfailureEvents() const {
  return d->_onfailure;
}