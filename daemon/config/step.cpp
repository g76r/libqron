/* Copyright 2013 Hallowyn and others.
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
#include "step.h"
#include <QSharedData>
#include "pf/pfnode.h"
#include "task.h"
#include "sched/scheduler.h"
#include "log/log.h"
#include "configutils.h"
#include "action/requesttaskaction.h"

class StepData : public QSharedData {
public:
  QString _id, _fqsn;
  Step::Kind _kind;
  Task _wokflow, _subtask;
  QPointer<Scheduler> _scheduler;
  QSet<QString> _predecessors;
  QList<EventSubscription> _onready;
  StepData() : _kind(Step::Unknown) { }
};

Step::Step() {
}

Step::Step(PfNode node, Scheduler *scheduler, Task workflow,
           QHash<QString,Task> oldTasks) {
  StepData *sd = new StepData;
  sd->_scheduler = scheduler;
  sd->_id = ConfigUtils::sanitizeId(node.contentAsString(), false);
  sd->_fqsn = workflow.fqtn()+":"+sd->_id;
  if (node.name() == "and") {
    sd->_kind = Step::AndJoin;
    foreach (PfNode child, node.childrenByName("onready"))
      scheduler->loadEventSubscription(
            sd->_fqsn, child, &sd->_onready, sd->_id, workflow);
    // LATER warn if onsuccess, onfailure, onfinish, onstart is defined
  } else if (node.name() == "or") {
    sd->_kind = Step::OrJoin;
    foreach (PfNode child, node.childrenByName("onready"))
      scheduler->loadEventSubscription(
            sd->_fqsn, child, &sd->_onready, sd->_id, workflow);
    // LATER warn if onsuccess, onfailure, onfinish, onstart is defined
  } else if (node.name() == "task") {
    sd->_kind = Step::SubTask;
    QString taskgroup = node.attribute("taskgroup");
    if (!taskgroup.isEmpty() && taskgroup != workflow.taskGroup().id())
      Log::warning() << "ignoring inconsistent taskgroup: " << node.toString();
    sd->_subtask = Task(node, scheduler, workflow.taskGroup(), oldTasks,
                        workflow);
    // TODO handle parameters overrinding in subtasks
    // TODO force = true for subtask
    if (sd->_subtask.isNull()) {
      Log::error() << "step with invalid subtask: " << node.toString();
      delete sd;
      return;
    }
    sd->_onready.append(
          EventSubscription(sd->_fqsn, "onready",
                            RequestTaskAction(scheduler, sd->_subtask.fqtn())));
    // LATER warn if onready is defined for a subtask step
  } else {
      Log::error() << "unsupported step kind: " << node.toString();
      delete sd;
      return;
  }
  d = sd;
}

Step::~Step() {
}

Step::Step(const Step &rhs) : d(rhs.d) {
}

Step &Step::operator=(const Step &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

bool Step::operator==(const Step &other) const {
  return (isNull() && other.isNull()) || fqsn() == other.fqsn();
}

QString Step::id() const {
  return d ? d->_id : QString();
}

QString Step::fqsn() const {
  return d ? d->_fqsn : QString();
}

Step::Kind Step::kind() const {
  return d ? d->_kind : Step::Unknown;
}

Task Step::subtask() const {
  return d ? d->_subtask : Task();
}

Task Step::workflow() const {
  return d ? d->_wokflow : Task();
}

QSet<QString> Step::predecessors() const {
  return d ? d->_predecessors : QSet<QString>();
}

void Step::insertPredecessor(QString predecessor) {
  if (d)
    d->_predecessors.insert(predecessor);
}

void Step::triggerReadyEvents(TaskInstance workflowTaskInstance) const {
  if (d)
    foreach (EventSubscription sub, d->_onready)
      sub.triggerActions(workflowTaskInstance);
}

QList<EventSubscription> Step::onreadyEventSubscriptions() const {
  return d ? d->_onready : QList<EventSubscription>();
}
