/* Copyright 2012-2015 Hallowyn and others.
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
#include <QHash>
#include "pf/pfnode.h"
#include "trigger/crontrigger.h"
#include "log/log.h"
#include <QAtomicInt>
#include <QPointer>
#include "sched/scheduler.h"
#include "config/configutils.h"
#include "requestformfield.h"
#include "util/htmlutils.h"
#include "step.h"
#include "action/action.h"
#include "trigger/noticetrigger.h"
#include "config/eventsubscription.h"
#include "sched/stepinstance.h"
#include "ui/graphvizdiagramsbuilder.h"
#include "ui/qronuiutils.h"
#include "task_p.h"
#include "modelview/shareduiitemdocumentmanager.h"

class WorkflowTriggerSubscriptionData : public QSharedData {
public:
  Trigger _trigger;
  EventSubscription _eventSubscription;
  WorkflowTriggerSubscriptionData(
      Trigger trigger = Trigger(),
      EventSubscription eventSubscription = EventSubscription())
    : _trigger(trigger), _eventSubscription(eventSubscription) { }
};

WorkflowTriggerSubscription::WorkflowTriggerSubscription() {
}

WorkflowTriggerSubscription::WorkflowTriggerSubscription(
    Trigger trigger, EventSubscription eventSubscription)
  : d(new WorkflowTriggerSubscriptionData(trigger, eventSubscription)) {
}

WorkflowTriggerSubscription::WorkflowTriggerSubscription(
    const WorkflowTriggerSubscription &other) : d(other.d) {
}

WorkflowTriggerSubscription::~WorkflowTriggerSubscription() {
}

WorkflowTriggerSubscription &WorkflowTriggerSubscription::operator=(
    const WorkflowTriggerSubscription &other) {
  if (this != &other)
    d = other.d;
  return *this;
}

Trigger WorkflowTriggerSubscription::trigger() const {
  return d ? d->_trigger : Trigger();
}

EventSubscription WorkflowTriggerSubscription::eventSubscription() const {
  return d ? d->_eventSubscription : EventSubscription();
}

class TaskData : public SharedUiItemData {
public:
  QString _id, _localId, _label, _command, _target, _info;
  Task::Mean _mean;
  TaskGroup _group;
  ParamSet _params, _setenv, _unsetenv;
  QList<NoticeTrigger> _noticeTriggers;
  QHash<QString,qint64> _resources;
  int _maxInstances;
  QList<CronTrigger> _cronTriggers;
  QList<QRegExp> _stderrFilters;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure;
  QPointer<Scheduler> _scheduler;
  long long _maxExpectedDuration, _minExpectedDuration, _maxDurationBeforeAbort;
  Task::DiscardAliasesOnStart _discardAliasesOnStart;
  QList<RequestFormField> _requestFormFields;
  QStringList _otherTriggers; // guessed indirect triggers resulting from events
  QString _supertaskId;
  QHash<QString,Step> _steps;
  QString _graphvizWorkflowDiagram;
  QHash<QString,WorkflowTriggerSubscription>
  _workflowTriggerSubscriptionsById;
  QMultiHash<QString,WorkflowTriggerSubscription>
  _workflowTriggerSubscriptionsByNotice;
  QHash<QString,CronTrigger> _workflowCronTriggersById;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _lastExecution, _nextScheduledExecution;
  mutable QAtomicInt _instancesCount;
  mutable bool _enabled, _lastSuccessful;
  mutable int _lastReturnCode, _lastTotalMillis;

  TaskData() : _maxExpectedDuration(LLONG_MAX), _minExpectedDuration(0),
    _maxDurationBeforeAbort(LLONG_MAX),
    _discardAliasesOnStart(Task::DiscardAll),
    _lastExecution(LLONG_MIN), _nextScheduledExecution(LLONG_MIN),
    _enabled(true), _lastSuccessful(true), _lastReturnCode(-1),
    _lastTotalMillis(-1)  { }
  QDateTime lastExecution() const;
  QDateTime nextScheduledExecution() const;
  QString resourcesAsString() const;
  QString triggersAsString() const;
  QString triggersWithCalendarsAsString() const;
  bool triggersHaveCalendar() const;
  QVariant uiData(int section, int role) const;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 int role, const SharedUiItemDocumentManager *dm);
  Qt::ItemFlags uiFlags(int section) const;
  QVariant uiHeaderData(int section, int role) const;
  int uiSectionCount() const;
  QString id() const { return _id; }
  void setId(QString id) { _id = id; }
  QString idQualifier() const { return "task"; }
};

Task::Task() {
}

Task::Task(const Task &other) : SharedUiItem(other) {
}

Task::Task(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
           QString supertaskId, QHash<QString,Calendar> namedCalendars) {
  TaskData *d = new TaskData;
  d->_scheduler = scheduler;
  d->_localId =
      ConfigUtils::sanitizeId(node.contentAsString(),
                              supertaskId.isEmpty() ? ConfigUtils::TaskId
                                                    : ConfigUtils::SubTaskId);
  d->_label = node.attribute("label");
  d->_mean = meanFromString(node.attribute("mean", "local").trimmed()
                             .toLower());
  if (d->_mean == UnknownMean) {
    Log::error() << "task with invalid execution mean: "
                 << node.toString();
    delete d;
    return;
  }
  d->_command = node.attribute("command");
  d->_target =
      ConfigUtils::sanitizeId(node.attribute("target"), ConfigUtils::GroupId);
  // silently use "localhost" as target for "local" mean
  if (d->_target.isEmpty() && d->_mean == Local)
    d->_target = "localhost";
  d->_info = node.stringChildrenByName("info").join(" ");
  d->_id = taskGroup.id()+"."+d->_localId;
  d->_group = taskGroup;
  d->_maxInstances = node.attribute("maxinstances", "1").toInt();
  if (d->_maxInstances <= 0) {
    Log::error() << "invalid task maxinstances: " << node.toPf();
    delete d;
    return;
  }
  double f = node.doubleAttribute("maxexpectedduration", -1);
  d->_maxExpectedDuration = f < 0 ? LLONG_MAX : (long long)(f*1000);
  f = node.doubleAttribute("minexpectedduration", -1);
  d->_minExpectedDuration = f < 0 ? 0 : (long long)(f*1000);
  f = node.doubleAttribute("maxdurationbeforeabort", -1);
  d->_maxDurationBeforeAbort = f < 0 ? LLONG_MAX : (long long)(f*1000);
  d->_params.setParent(taskGroup.params());
  ConfigUtils::loadParamSet(node, &d->_params, "param");
  QString filter = d->_params.value("stderrfilter");
  if (!filter.isEmpty())
    d->_stderrFilters.append(QRegExp(filter));
  d->_setenv.setParent(taskGroup.setenv());
  ConfigUtils::loadParamSet(node, &d->_setenv, "setenv");
  d->_unsetenv.setParent(taskGroup.unsetenv());
  ConfigUtils::loadFlagSet(node, &d->_unsetenv, "unsetenv");
  d->_supertaskId = supertaskId;
  // LATER load cron triggers last exec timestamp from on-disk log
  if (supertaskId.isEmpty()) { // subtasks do not have triggers
    foreach (PfNode child, node.childrenByName("trigger")) {
      foreach (PfNode grandchild, child.children()) {
        QString content = grandchild.contentAsString();
        QString triggerType = grandchild.name();
        if (triggerType == "notice") {
          NoticeTrigger trigger(grandchild, namedCalendars);
          if (trigger.isValid()) {
            d->_noticeTriggers.append(trigger);
            Log::debug() << "configured notice trigger '" << content
                         << "' on task '" << d->_localId << "'";
          } else {
            Log::error() << "task with invalid notice trigger: "
                         << node.toString();
            delete d;
            return;
          }
        } else if (triggerType == "cron") {
          CronTrigger trigger(grandchild, namedCalendars);
          if (trigger.isValid()) {
            d->_cronTriggers.append(trigger);
            Log::debug() << "configured cron trigger "
                         << trigger.humanReadableExpression()
                         << " on task " << d->_localId;
          } else {
            Log::error() << "task with invalid cron trigger: "
                         << node.toString();
            delete d;
            return;
          }
          // LATER read misfire config
        } else {
          Log::warning() << "ignoring unknown trigger type '" << triggerType
                         << "' on task " << d->_localId;
        }
      }
    }
  } else {
    if (node.hasChild("trigger"))
      Log::warning() << "ignoring trigger in workflow subtask: "
                     << node.toString();
  }
  ConfigUtils::loadResourcesSet(node, &d->_resources, "resource");
  QString daos = node.attribute("discardaliasesonstart", "all");
  d->_discardAliasesOnStart = discardAliasesOnStartFromString(daos);
  if (d->_discardAliasesOnStart == Task::DiscardUnknown) {
    Log::error() << "invalid discardaliasesonstart on task " << d->_localId
                 << ": '" << daos << "'";
    delete d;
    return;
  }
  QList<PfNode> children = node.childrenByName("requestform");
  if (!children.isEmpty()) {
    if (children.size() > 1) {
      Log::error() << "task with several requestform: " << node.toString();
      delete d;
      return;
    }
    foreach (PfNode child, children.last().childrenByName("field")) {
      RequestFormField field(child);
      if (!field.isNull())
        d->_requestFormFields.append(field);
    }
  }
  ConfigUtils::loadEventSubscription(node, "onstart", d->_id, &d->_onstart,
                                     scheduler);
  ConfigUtils::loadEventSubscription(node, "onsuccess", d->_id,
                                     &d->_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(node, "onfinish", d->_id,
                                     &d->_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(node, "onfailure", d->_id,
                                     &d->_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(node, "onfinish", d->_id,
                                     &d->_onfailure, scheduler);
  QList<PfNode> steps = node.childrenByName("subtask")
      +node.childrenByName("and")+node.childrenByName("or");
  if (d->_mean == Workflow) {
    if (steps.isEmpty())
      Log::warning() << "workflow task with no step: " << node.toString();
    if (!supertaskId.isEmpty()) {
      Log::error() << "workflow task not allowed as a workflow subtask: "
                   << node.toString();
      delete d;
      return;
    }
    foreach (PfNode child, steps) {
      Step step(child, scheduler, taskGroup, d->_id, namedCalendars);
      if (step.isNull()) {
        Log::error() << "workflow task " << d->_id
                     << " has at less one invalid step definition";
        delete d;
        return;
      } if (d->_steps.contains(step.id())) {
        Log::error() << "workflow task " << d->_id
                     << " has duplicate steps with id " << step.id();
        delete d;
        return;
      } else {
        d->_steps.insert(step.id(), step);
      }
    }
    QStringList startSteps = node.stringListAttribute("start").toSet().toList();
    qSort(startSteps);
    Step startStep(PfNode("start"), scheduler, taskGroup, d->_id,
                   namedCalendars);
    Step endStep(PfNode("end"), scheduler, taskGroup, d->_id,
                 namedCalendars);
    //Log::debug() << "steps: " << d->_steps.keys() << " startsteps: "
    //             << startSteps;
    foreach (QString localId, startSteps) {
      QString id = d->_id+":"+localId;
      if (!d->_steps.contains(id)) {
        Log::error() << "workflow task " << d->_id
                     << " has invalid or lacking start steps: "
                     << node.toString();
        delete d;
        return;
      }
      startStep.appendOnReadyStep(scheduler, localId);
    }
    d->_steps.insert(startStep.id(), startStep);
    d->_steps.insert(endStep.id(), endStep);
    int tsCount = 0;
    QStringList ignoredChildren;
    ignoredChildren << "cron" << "notice";
    foreach (PfNode child, node.childrenByName("ontrigger")) {
      EventSubscription es(d->_id, child, scheduler, ignoredChildren);
      if (es.isNull() || es.actions().isEmpty()) {
        Log::warning() << "ignoring invalid or empty ontrigger: "
                       << node.toString();
        continue;
      }
      foreach (PfNode grandchild, child.childrenByName("cron")) {
        QString tsId = QString::number(tsCount++);
        CronTrigger trigger(grandchild, namedCalendars);
        if (trigger.isValid()) {
          d->_workflowCronTriggersById.insert(tsId, trigger);
          d->_workflowTriggerSubscriptionsById.insert(
                tsId, WorkflowTriggerSubscription(trigger, es));
        } else
          Log::warning() << "ignoring invalid cron trigger in ontrigger: "
                         << node.toString();
      }
      foreach (PfNode grandchild, child.childrenByName("notice")) {
        QString tsId = QString::number(tsCount++);
        NoticeTrigger trigger(grandchild, namedCalendars);
        if (trigger.isValid()) {
          d->_workflowTriggerSubscriptionsByNotice
              .insert(trigger.expression(),
                      WorkflowTriggerSubscription(trigger, es));
          d->_workflowTriggerSubscriptionsById.insert(
                tsId, WorkflowTriggerSubscription(trigger, es));
        } else
          Log::warning() << "ignoring invalid notice trigger in ontrigger: "
                         << node.toString();
      }
    }
    // LATER replace the following predecessor/transition string with a structured data type
    // Note about predecessors' transitionId conventions:
    // The general format about predecessor/transition string is the following:
    //   format: ${source_step_id}|${event_name}|${target_step_local_id}
    //   e.g.: group1.workflow1:step1|onfinish|step2
    //         group1.workflow1:step4|onready|$end
    // However there is a special handling for "onsuccess" and "onfailure"
    // events, that are replaced with "onfinish" to ensure that there is only
    // one predecessor to an "and" step that would be following a "onfinish"
    // event subscription, or both the "onsuccess" and "onfailure" from the
    // same subtask step.
    foreach (QString source, d->_steps.keys()) {
      QList<EventSubscription> subList
          = d->_steps[source].onreadyEventSubscriptions()
          + d->_steps[source].subtask().allEventsSubscriptions()
          + d->_steps[source].subtask().taskGroup().allEventSubscriptions();
      foreach (EventSubscription es, subList) {
        foreach (Action a, es.actions()) {
          if (a.actionType() == "step") {
            QString targetLocalId = a.targetName();
            QString eventName = es.eventName();
            if (eventName == "onsuccess" || eventName == "onfailure")
              eventName = "onfinish";
            QString transitionId;
            transitionId = source+"|"+eventName+"|"+targetLocalId;
            QString targetId = d->_id+":"+targetLocalId;
            if (d->_steps.contains(targetId)) {
              Log::debug() << "registring predecessor " << transitionId
                           << " to step " << d->_steps[targetId].id();
              d->_steps[targetId].insertPredecessor(transitionId);
            } else {
              Log::error() << "cannot register predecessor " << transitionId
                           << " with unknown target to workflow " << d->_id;
              delete d;
              return;
            }
          }
        }
      }
    }
    // TODO reject workflows w/ no start edge
    // TODO reject workflows w/ step w/o predecessors
    // TODO reject workflows w/ step w/o successors, at less on trivial cases (e.g. neither step nor end in onfailure)
  } else {
    if (!steps.isEmpty() || node.hasChild("start"))
      Log::warning() << "ignoring step definitions in non-workflow task: "
                     << node.toString();
  }
  setData(d);
  d->_graphvizWorkflowDiagram = GraphvizDiagramsBuilder::workflowTaskDiagram(*this);
}

void Task::copyLiveAttributesFromOldTask(Task oldTask) {
  TaskData *d = this->data();
  if (!d || oldTask.isNull())
    return;
  // copy mutable fields from old task (excepted _nextScheduledExecution)
  d->_lastExecution = oldTask.lastExecution().isValid()
      ? oldTask.lastExecution().toMSecsSinceEpoch() : LLONG_MIN;
  d->_instancesCount = oldTask.instancesCount();
  d->_lastSuccessful = oldTask.lastSuccessful();
  d->_lastReturnCode = oldTask.lastReturnCode();
  d->_lastTotalMillis = oldTask.lastTotalMillis();
  d->_enabled = oldTask.enabled();
  // keep last triggered timestamp from previously defined trigger
  QHash<QString,CronTrigger> oldCronTriggers;
  foreach (const CronTrigger trigger, oldTask.data()->_cronTriggers)
    oldCronTriggers.insert(trigger.canonicalExpression(), trigger);
  for (int i = 0; i < d->_cronTriggers.size(); ++i) {
    CronTrigger &trigger = d->_cronTriggers[i];
    CronTrigger oldTrigger =
        oldCronTriggers.value(trigger.canonicalExpression());
    if (oldTrigger.isValid())
      trigger.setLastTriggered(oldTrigger.lastTriggered());
  }
}

Task Task::templateTask() {
  Task t;
  t.setData(new TaskData);
  return t;
}

ParamSet Task::params() const {
  return !isNull() ? data()->_params : ParamSet();
}

QList<NoticeTrigger> Task::noticeTriggers() const {
  return !isNull() ? data()->_noticeTriggers : QList<NoticeTrigger>();
}

QString Task::localId() const {
  return !isNull() ? data()->_localId : QString();
}

QString Task::label() const {
  return !isNull() ? (data()->_label.isNull() ? data()->_localId : data()->_label)
                   : QString();
}

Task::Mean Task::mean() const {
  return !isNull() ? data()->_mean : UnknownMean;
}

QString Task::command() const {
  return !isNull() ? data()->_command : QString();
}

QString Task::target() const {
  return !isNull() ? data()->_target : QString();
}

void Task::setTarget(QString target) {
  if (!isNull())
    data()->_target = target;
}

QString Task::info() const {
  return !isNull() ? data()->_info : QString();
}

TaskGroup Task::taskGroup() const {
  return !isNull() ? data()->_group : TaskGroup();
}

void Task::setTaskGroup(TaskGroup taskGroup) {
  if (!isNull())
    data()->_group = taskGroup;
}

QHash<QString, qint64> Task::resources() const {
  return !isNull() ? data()->_resources : QHash<QString,qint64>();
}

QString Task::resourcesAsString() const {
  return !isNull() ? data()->resourcesAsString() : QString();
}

QString TaskData::resourcesAsString() const {
  return QronUiUtils::resourcesAsString(_resources);
}

QString Task::triggersAsString() const {
  return !isNull() ? data()->triggersAsString() : QString();
}

QString TaskData::triggersAsString() const {
  QString s;
  foreach (CronTrigger t, _cronTriggers)
    s.append(t.humanReadableExpression()).append(' ');
  foreach (NoticeTrigger t, _noticeTriggers)
    s.append(t.humanReadableExpression()).append(' ');
  foreach (QString t, _otherTriggers)
    s.append(t).append(' ');
  s.chop(1); // remove last space
  return s;
}

QString Task::triggersWithCalendarsAsString() const {
  return !isNull() ? data()->triggersWithCalendarsAsString() : QString();
}

QString TaskData::triggersWithCalendarsAsString() const {
  QString s;
  foreach (CronTrigger t, _cronTriggers)
    s.append(t.humanReadableExpressionWithCalendar()).append(' ');
  foreach (NoticeTrigger t, _noticeTriggers)
    s.append(t.humanReadableExpressionWithCalendar()).append(' ');
  foreach (QString t, _otherTriggers)
    s.append(t).append(" ");
  s.chop(1); // remove last space
  return s;
}

bool Task::triggersHaveCalendar() const {
  return !isNull() ? data()->triggersHaveCalendar() : false;
}

bool TaskData::triggersHaveCalendar() const {
  foreach (CronTrigger t, _cronTriggers)
    if (!t.calendar().isNull())
      return true;
  foreach (NoticeTrigger t, _noticeTriggers)
    if (!t.calendar().isNull())
      return true;
  return false;
}

QDateTime Task::lastExecution() const {
  return !isNull() ? data()->lastExecution() : QDateTime();
}

QDateTime TaskData::lastExecution() const {
  return _lastExecution != LLONG_MIN
      ? QDateTime::fromMSecsSinceEpoch(_lastExecution) : QDateTime();
}

QDateTime Task::nextScheduledExecution() const {
  return !isNull() ? data()->nextScheduledExecution() : QDateTime();
}

QDateTime TaskData::nextScheduledExecution() const {
  return _nextScheduledExecution != LLONG_MIN
      ? QDateTime::fromMSecsSinceEpoch(_nextScheduledExecution) : QDateTime();
}

void Task::setLastExecution(QDateTime timestamp) const {
  if (!isNull())
    data()->_lastExecution = timestamp.isValid()
        ? timestamp.toMSecsSinceEpoch() : LLONG_MIN;
}

void Task::setNextScheduledExecution(QDateTime timestamp) const {
  if (!isNull())
    data()->_nextScheduledExecution = timestamp.isValid()
        ? timestamp.toMSecsSinceEpoch() : LLONG_MIN;
}

int Task::maxInstances() const {
  return !isNull() ? data()->_maxInstances : 0;
}

int Task::instancesCount() const {
  return !isNull() ? data()->_instancesCount.load() : 0;
}

int Task::fetchAndAddInstancesCount(int valueToAdd) const {
  return !isNull() ? data()->_instancesCount.fetchAndAddOrdered(valueToAdd) : 0;
}

QList<QRegExp> Task::stderrFilters() const {
  return !isNull() ? data()->_stderrFilters : QList<QRegExp>();
}

void Task::appendStderrFilter(QRegExp filter) {
  if (!isNull())
    data()->_stderrFilters.append(filter);
}

void Task::triggerStartEvents(TaskInstance instance) const {
  if (!isNull()) {
    data()->_group.triggerStartEvents(instance);
    foreach (EventSubscription sub, data()->_onstart)
      sub.triggerActions(instance);
  }
}

void Task::triggerSuccessEvents(TaskInstance instance) const {
  if (!isNull()) {
    data()->_group.triggerSuccessEvents(instance);
    foreach (EventSubscription sub, data()->_onsuccess)
      sub.triggerActions(instance);
  }
}

void Task::triggerFailureEvents(TaskInstance instance) const {
  if (!isNull()) {
    data()->_group.triggerFailureEvents(instance);
    foreach (EventSubscription sub, data()->_onfailure)
      sub.triggerActions(instance);
  }
}

QList<EventSubscription> Task::onstartEventSubscriptions() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> Task::onsuccessEventSubscriptions() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> Task::onfailureEventSubscriptions() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

QList<EventSubscription> Task::allEventsSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onstart + data()->_onsuccess + data()->_onfailure
                   : QList<EventSubscription>();
}

bool Task::enabled() const {
  return !isNull() ? data()->_enabled : false;
}
void Task::setEnabled(bool enabled) const {
  if (!isNull())
    data()->_enabled = enabled;
}

bool Task::lastSuccessful() const {
  return !isNull() ? data()->_lastSuccessful : false;
}

void Task::setLastSuccessful(bool successful) const {
  if (!isNull())
    data()->_lastSuccessful = successful;
}

int Task::lastReturnCode() const {
  return !isNull() ? data()->_lastReturnCode : -1;
}

void Task::setLastReturnCode(int code) const {
  if (!isNull())
    data()->_lastReturnCode = code;
}

int Task::lastTotalMillis() const {
  return !isNull() ? data()->_lastTotalMillis : -1;
}

void Task::setLastTotalMillis(int lastTotalMillis) const {
  if (!isNull())
    data()->_lastTotalMillis = lastTotalMillis;
}

long long Task::maxExpectedDuration() const {
  return !isNull() ? data()->_maxExpectedDuration : LLONG_MAX;
}
long long Task::minExpectedDuration() const {
  return !isNull() ? data()->_minExpectedDuration : 0;
}

long long Task::maxDurationBeforeAbort() const {
  return !isNull() ? data()->_maxDurationBeforeAbort : LLONG_MAX;
}

ParamSet Task::setenv() const {
  return !isNull() ? data()->_setenv : ParamSet();
}

ParamSet Task::unsetenv() const {
  return !isNull() ? data()->_unsetenv : ParamSet();
}

Task::DiscardAliasesOnStart Task::discardAliasesOnStart() const {
  return !isNull() ? data()->_discardAliasesOnStart : Task::DiscardNone;
}

QString Task::discardAliasesOnStartAsString(Task::DiscardAliasesOnStart v) {
  switch (v) {
  case Task::DiscardNone:
    return "none";
  case Task::DiscardAll:
    return "all";
  case Task::DiscardUnknown:
    ;
  }
  return "unknown"; // should never happen
}

Task::DiscardAliasesOnStart Task::discardAliasesOnStartFromString(QString v) {
  if (v == "none")
    return Task::DiscardNone;
  if (v == "all")
    return Task::DiscardAll;
  return Task::DiscardUnknown;
}

QList<RequestFormField> Task::requestFormFields() const {
  return !isNull() ? data()->_requestFormFields : QList<RequestFormField>();
}

QString Task::requestFormFieldsAsHtmlDescription() const {
  QList<RequestFormField> list = requestFormFields();
  if (list.isEmpty())
    return "(none)";
  QString v;
  foreach (const RequestFormField rff, list)
    v.append(rff.toHtmlHumanReadableDescription());
  return v;
}

QVariant TaskPseudoParamsProvider::paramValue(
    QString key, QVariant defaultValue) const {
  //Log::fatal() << "TaskPseudoParamsProvider::paramValue " << key;
  if (key.startsWith('!')) {
    if (key == "!tasklocalid") {
      return _task.localId();
    } else if (key == "!taskid") {
      return _task.id();
    } else if (key == "!taskgroupid") {
      return _task.taskGroup().id();
    } else if (key == "!target") {
      return _task.target();
    } else if (key == "!minexpectedms") {
      return _task.minExpectedDuration();
    } else if (key == "!minexpecteds") {
      return _task.minExpectedDuration()/1000.0;
    } else if (key == "!maxexpectedms") {
      long long ms = _task.maxExpectedDuration();
      return (ms == LLONG_MAX) ? defaultValue : ms;
    } else if (key == "!maxexpecteds") {
      long long ms = _task.maxExpectedDuration();
      return (ms == LLONG_MAX) ? defaultValue : ms/1000.0;
    } else if (key == "!maxbeforeabortms") {
      long long ms = _task.maxDurationBeforeAbort();
      return (ms == LLONG_MAX) ? defaultValue : ms;
    } else if (key == "!maxbeforeaborts") {
      long long ms = _task.maxDurationBeforeAbort();
      return (ms == LLONG_MAX) ? defaultValue : ms/1000.0;
    } else if (key == "!maxexpectedms0") {
      long long ms = _task.maxExpectedDuration();
      return (ms == LLONG_MAX) ? 0 : ms;
    } else if (key == "!maxexpecteds0") {
      long long ms = _task.maxExpectedDuration();
      return (ms == LLONG_MAX) ? 0.0 : ms/1000.0;
    } else if (key == "!maxbeforeabortms0") {
      long long ms = _task.maxDurationBeforeAbort();
      return (ms == LLONG_MAX) ? 0 : ms;
    } else if (key == "!maxbeforeaborts0") {
      long long ms = _task.maxDurationBeforeAbort();
      return (ms == LLONG_MAX) ? 0.0 : ms/1000.0;
    }
  }
  return defaultValue;
}

QList<CronTrigger> Task::cronTriggers() const {
  return !isNull() ? data()->_cronTriggers : QList<CronTrigger>();
}

QStringList Task::otherTriggers() const {
  return !isNull() ? data()->_otherTriggers : QStringList();
}

void Task::appendOtherTriggers(QString text) {
  if (!isNull())
    data()->_otherTriggers.append(text);
}

void Task::clearOtherTriggers() {
  if (!isNull())
    data()->_otherTriggers.clear();
}

QHash<QString, Step> Task::steps() const {
  return !isNull() ? data()->_steps : QHash<QString,Step>();
}

QString Task::supertaskId() const {
  return !isNull() ? data()->_supertaskId : QString();
}

QString Task::graphvizWorkflowDiagram() const {
  return !isNull() ? data()->_graphvizWorkflowDiagram : QString();
}

QHash<QString,WorkflowTriggerSubscription>
Task::workflowTriggerSubscriptionsById() const {
  return !isNull() ? data()->_workflowTriggerSubscriptionsById
                   : QHash<QString,WorkflowTriggerSubscription>();
}

QMultiHash<QString, WorkflowTriggerSubscription>
Task::workflowTriggerSubscriptionsByNotice() const {
  return !isNull() ? data()->_workflowTriggerSubscriptionsByNotice
                   : QMultiHash<QString,WorkflowTriggerSubscription>();
}

QHash<QString,CronTrigger> Task::workflowCronTriggersById() const {
  return !isNull() ? data()->_workflowCronTriggersById
                   : QHash<QString,CronTrigger>();
}

QVariant TaskData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int TaskData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

QVariant TaskData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      if (role == Qt::EditRole) {
        // remove workflow id prefix from localid
        return _supertaskId.isEmpty() ? _localId
                                      : _localId.mid(_localId.indexOf(':')+1);
      }
      return _localId;
    case 1:
      return _group.id();
    case 2:
      if (role == Qt::EditRole)
        return _label == _localId ? QVariant() : _label;
      return _label.isEmpty() ? _localId : _label;
    case 3:
      return Task::meanAsString(_mean);
    case 4:
      return _command;
    case 5:
      return _target;
    case 6:
      return triggersAsString();
    case 7:
      return _params.toString(false, false);
    case 8:
      return resourcesAsString();
    case 9:
      return lastExecution().toString("yyyy-MM-dd hh:mm:ss,zzz");
    case 10:
      return nextScheduledExecution().toString("yyyy-MM-dd hh:mm:ss,zzz");
    case 11:
      return _id;
    case 12:
      return _maxInstances;
    case 13:
      return _instancesCount.load();
    case 14:
      return EventSubscription::toStringList(_onstart).join("\n");
    case 15:
      return EventSubscription::toStringList(_onsuccess).join("\n");
    case 16:
      return EventSubscription::toStringList(_onfailure).join("\n");
    case 17:
      return QString::number(_instancesCount.load())+" / "
          +QString::number(_maxInstances);
    case 18:
      return QVariant(); // custom actions, handled by the model, if needed
    case 19: {
      QDateTime dt = lastExecution();
      if (dt.isNull())
        return QVariant();
      QString returnCode = QString::number(_lastReturnCode);
      QString returnCodeLabel =
          _params.value("return.code."+returnCode+".label");
      QString s = dt.toString("yyyy-MM-dd hh:mm:ss,zzz")
          .append(_lastSuccessful ? " success" : " failure")
          .append(" (code ").append(returnCode);
      if (!returnCodeLabel.isEmpty())
        s.append(" : ").append(returnCodeLabel);
      return s.append(')');
    }
    case 20:
      return QronUiUtils::sysenvAsString(_setenv, _unsetenv);
    case 21:
      return QronUiUtils::paramsAsString(_setenv);
    case 22:
      return QronUiUtils::paramsKeysAsString(_unsetenv);
    case 23:
      return (_minExpectedDuration > 0)
          ? QString::number(_minExpectedDuration*.001) : QString();
    case 24:
      return (_maxExpectedDuration < LLONG_MAX)
          ? QString::number(_maxExpectedDuration*.001) : QString();
    case 25: {
      QString s;
      foreach (const RequestFormField rff, _requestFormFields)
        s.append(rff.id()).append(' ');
      s.chop(1);
      return s;
    }
    case 26:
      return _lastTotalMillis >= 0
          ? QString::number(_lastTotalMillis/1000.0) : QString();
    case 27:
      return (_maxDurationBeforeAbort < LLONG_MAX)
          ? QString::number(_maxDurationBeforeAbort*.001) : QString();
    case 28:
      return triggersWithCalendarsAsString();
    case 29:
      return _enabled;
    case 30:
      return triggersHaveCalendar();
    case 31:
      return _supertaskId;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

void Task::setSuperTaskId(QString supertaskId) {
  if (!isNull()) {
    TaskData *d = data();
    d->_supertaskId = supertaskId;
    d->_localId = supertaskId.mid(supertaskId.lastIndexOf('.')+1)+":"
        +d->_localId.mid(d->_localId.indexOf(':')+1);
  }
}

bool Task::setUiData(int section, const QVariant &value, QString *errorString,
                     int role, const SharedUiItemDocumentManager *dm) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, role, dm);
}

bool TaskData::setUiData(int section, const QVariant &value,
                         QString *errorString, int role,
                         const SharedUiItemDocumentManager *dm) {
  if (!dm) {
    if (errorString)
      *errorString = "cannot set ui data without document manager";
    return false;
  }
  if (role != Qt::EditRole) {
    if (errorString)
      *errorString = "cannot set other role than EditRole";
    return false;
  }
  QString s = value.toString().trimmed(), s2;
  switch(section) {
  case 0:
    if (value.toString().isEmpty()) {
      if (errorString)
        *errorString = "id cannot be empty";
      return false;
    }
    s = ConfigUtils::sanitizeId(s, ConfigUtils::TaskId);
    if (!_supertaskId.isEmpty())
      s = _supertaskId.mid(_supertaskId.lastIndexOf('.')+1)+":"+s;
    s2 = _group.id()+"."+s;
    if (!dm->itemById("task", s2).isNull()) {
      if (errorString)
        *errorString = "New id is already used by another task: "+s;
      return false;
    }
    _localId = s;
    _id = s2;
    return true;
  case 1: {
    SharedUiItem group = dm->itemById("taskgroup", s);
    if (group.isNull()) {
      if (errorString)
        *errorString = "No group with such id: \""+s+"\"";
      return false;
    }
    if (!_supertaskId.isEmpty()) {
      SharedUiItem supertaskItem = dm->itemById("task", _supertaskId);
      if (!supertaskItem.isNull()) {
        Task supertask = reinterpret_cast<Task&>(supertaskItem);
        if (s != supertask.taskGroup().id()) {
          if (errorString)
            *errorString = "Cannot make a subtask belong to another group than "
                           "its parent task's group: \""+s+"\" instead of \""
              +supertask.taskGroup().id()+"\"";
          return false;
        }
      }
    }
    _group = reinterpret_cast<TaskGroup&>(group);
    return true;
  }
  case 2:
    _label = value.toString().trimmed();
    if (_label == _localId)
      _label = QString();
    return true;
  case 3: {
    Task::Mean mean = Task::meanFromString(value.toString().toLower()
                                           .trimmed());
    if (mean == Task::UnknownMean) {
      if (errorString)
        *errorString = "unknown mean value: '"+value.toString()+"'";
      return false;
    }
    _mean = mean;
    return true;
  }
  case 5:
    _target = ConfigUtils::sanitizeId(value.toString(), ConfigUtils::GroupId);
    return true;
  case 8: {
    QHash<QString,qint64> resources;
    if (QronUiUtils::resourcesFromString(value.toString(), &resources,
                                            errorString)) {
      _resources = resources;
      return true;
    }
    return false;
  }
  }
  if (errorString)
    *errorString = "field \""+uiHeaderData(section, Qt::DisplayRole).toString()
      +"\" is not ui-editable";
  return false;
}

Qt::ItemFlags TaskData::uiFlags(int section) const {
  Q_UNUSED(section)
  // TODO more flags, maybe not same ones for every section
  // FIXME mark only editable sections as editable
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void Task::setParentParams(ParamSet parentParams) {
  if (!isNull())
    data()->_params.setParent(parentParams);
}

TaskData *Task::data() {
  detach<TaskData>();
  return (TaskData*)SharedUiItem::data();
}

PfNode Task::toPfNode() const {
  const TaskData *d = data();
  if (!d)
    return PfNode();
  PfNode node("task", d->_localId);

  // description and execution attributes
  node.setAttribute("taskgroup", d->_group.id());
  if (!d->_label.isEmpty() && d->_label != d->_id)
    node.setAttribute("label", d->_label);
  if (!d->_info.isEmpty())
    node.setAttribute("info", d->_info);
  node.setAttribute("mean", meanAsString(d->_mean));
  // do not set target attribute if it is empty,
  // or in case it is implicit ("localhost" for Local mean)
  if (!d->_target.isEmpty()
      && (d->_target != "localhost" || !d->_mean == Local))
    node.setAttribute("target", d->_target);
  // do not set command attribute if it is empty
  // or for means that do not use it (Workflow and DoNothing)
  if (!d->_command.isEmpty()
      && d->_mean != DoNothing
      && d->_mean != Workflow)
    node.setAttribute("command", d->_command);

  // triggering and constraints attributes
  PfNode triggers("trigger");
  foreach (const Trigger &ct, d->_cronTriggers)
    triggers.appendChild(ct.toPfNode());
  foreach (const Trigger &nt, d->_noticeTriggers)
    triggers.appendChild(nt.toPfNode());
  node.appendChild(triggers);
  if (d->_discardAliasesOnStart != DiscardAll)
    node.appendChild(
          PfNode("discardaliasesonstart",
                 discardAliasesOnStartAsString(d->_discardAliasesOnStart)));
  if (d->_maxInstances != 1)
    node.appendChild(PfNode("maxinstances",
                            QString::number(d->_maxInstances)));
  foreach (const QString &key, d->_resources.keys())
    node.appendChild(
          PfNode("resource",
                 key+" "+QString::number(d->_resources.value(key))));

  // workflow attributes
  Step startStep = d->_steps.value(d->id()+":$start");
  QSet<QString> startSteps;
  foreach (const EventSubscription &es, startStep.onreadyEventSubscriptions())
    foreach (const Action &a, es.actions()) {
      PfNode node = a.toPfNode();
      if (node.name() == "step") { // should always be true
        QString stepLocalId = node.contentAsString();
        if (!stepLocalId.isEmpty()) // should always be true
          startSteps.insert(stepLocalId);
      }
    }
  if (startSteps.size())
    ConfigUtils::writeFlagSet(&node, startSteps, "start");
  if (!d->_workflowTriggerSubscriptionsById.isEmpty()) {
    foreach (const WorkflowTriggerSubscription &wts,
             d->_workflowTriggerSubscriptionsById.values()) {
      PfNode ontrigger = wts.eventSubscription().toPfNode();
      ontrigger.prependChild(wts.trigger().toPfNode());
      node.appendChild(ontrigger);
    }
  }
  QList<Step> steps = d->_steps.values();
  qSort(steps);
  foreach (const Step &step, steps)
    if (!step.id().contains('$'))
      node.appendChild(step.toPfNode());

  // params and setenv attributes
  ConfigUtils::writeParamSet(&node, d->_params, "param");
  ConfigUtils::writeParamSet(&node, d->_setenv, "setenv");
  ConfigUtils::writeFlagSet(&node, d->_unsetenv, "unsetenv");

  // monitoring and alerting attributes
  if (d->_maxExpectedDuration < LLONG_MAX)
    node.appendChild(PfNode("maxexpectedduration",
                            QString::number(d->_maxExpectedDuration)));
  if (d->_minExpectedDuration > 0)
    node.appendChild(PfNode("minexpectedduration",
                            QString::number(d->_minExpectedDuration)));
  if (d->_maxDurationBeforeAbort < LLONG_MAX)
    node.appendChild(PfNode("maxdurationbeforeabort",
                            QString::number(d->_maxDurationBeforeAbort)));

  // events (but workflow-specific "ontrigger" events)
  ConfigUtils::writeEventSubscriptions(&node, d->_onstart);
  ConfigUtils::writeEventSubscriptions(&node, d->_onsuccess);
  ConfigUtils::writeEventSubscriptions(&node, d->_onfailure,
                                       QStringList("onfinish"));

  // user interface attributes
  if (!d->_requestFormFields.isEmpty()) {
    PfNode requestForm("requestform");
    foreach (const RequestFormField &field, d->_requestFormFields)
      requestForm.appendChild(field.toPfNode());
    node.appendChild(requestForm);
  }
  return node;
}

Task::Mean Task::meanFromString(QString mean) {
  if (mean == "donothing")
    return DoNothing;
  if (mean == "local")
    return Local;
  if (mean == "workflow")
    return Workflow;
  if (mean == "ssh")
    return Ssh;
  if (mean == "http")
    return Http;
  return UnknownMean;
}

QString Task::meanAsString(Task::Mean mean) {
  // LATER optimize with const QStrings
  switch(mean) {
  case DoNothing:
    return "donothing";
  case Local:
    return "local";
  case Workflow:
    return "workflow";
  case Ssh:
    return "ssh";
  case Http:
    return "http";
  case UnknownMean:
    ;
  }
  return QString();
}

QStringList Task::validMeanStrings() {
  QStringList means;
  means << "donothing" << "local" << "workflow" << "ssh" << "http";
  return means;
}
