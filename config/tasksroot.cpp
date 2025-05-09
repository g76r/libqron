/* Copyright 2022-2025 Gregoire Barbier and others.
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
#include "tasksroot.h"
#include "task_p.h"

TasksRoot::TasksRoot() {
}

TasksRoot::TasksRoot(const TasksRoot &other) : SharedUiItem(other) {
}

TasksRoot::TasksRoot(PfNode node, Scheduler *scheduler) {
  TasksRootData *d = new TasksRootData;
  if (d->loadConfig(node, scheduler)) {
    setData(d);
  } else {
    delete d;
  }
}

bool TasksRootData::loadConfig(
    PfNode node, Scheduler *scheduler) {
  _originalPfNodes += node;
  _params += ParamSet(node, "param");
  _vars += ParamSet(node, "var");
  _vars.setScope("var");
  _instanceparams += ParamSet(node, "instanceparam");
  _instanceparams.setScope("instanceparams");
  ConfigUtils::loadBoolean(node, "mergestdoutintostderr",
                           &_mergeStdoutIntoStderr);
  ConfigUtils::loadEventSubscription(
      node, "onplan", TASKSROOTID, &_onplan, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstart", TASKSROOTID, &_onstart, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onsuccess", TASKSROOTID, &_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfinish", TASKSROOTID, &_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfailure", TASKSROOTID, &_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfinish", TASKSROOTID, &_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstderr", TASKSROOTID, &_onstderr, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstdout", TASKSROOTID, &_onstdout, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onnostderr", TASKSROOTID, &_onnostderr, scheduler);
  return true;
}

ParamSet TasksRoot::params() const {
  return !isNull() ? data()->_params : ParamSet();
}

ParamSet TasksRoot::vars() const {
  return !isNull() ? data()->_vars : ParamSet();
}

ParamSet TasksRoot::instanceparams() const {
  return !isNull() ? data()->_instanceparams : ParamSet();
}

QList<EventSubscription> TasksRoot::onplan() const {
  return !isNull() ? data()->_onplan : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstart() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onsuccess() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onfailure() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstderr() const {
  return !isNull() ? data()->_onstderr : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstdout() const {
  return !isNull() ? data()->_onstdout : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onnostderr() const {
  return !isNull() ? data()->_onnostderr : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::allEventSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onplan + data()->_onstart + data()->_onsuccess
                         + data()->_onfailure
                         + data()->_onstderr + data()->_onstdout
                         + data()->_onnostderr
                   : QList<EventSubscription>();
}

bool TasksRoot::mergeStdoutIntoStderr() const {
  return !isNull() ? data()->_mergeStdoutIntoStderr : false;
}

QVariant TasksRootData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 7:
      return _params.toString(false, false);
    case 14:
      return EventSubscription::toStringList(_onstart).join("\n");
    case 15:
      return EventSubscription::toStringList(_onsuccess).join("\n");
    case 16:
      return EventSubscription::toStringList(_onfailure).join("\n");
    case 19:
      return EventSubscription::toStringList(_onnostderr).join("\n");
    case 21:
      return _vars.toString(false, false);
    case 22:
      return _instanceparams.toString(false, false);
    case 36:
      return EventSubscription::toStringList(_onplan).join("\n");
    case 38:
      return _mergeStdoutIntoStderr;
    case 39:
      return EventSubscription::toStringList(_onstderr).join("\n");
    case 40:
      return EventSubscription::toStringList(_onstdout).join("\n");
    }
    break;
  default:
      ;
  }
  return QVariant();
}

TasksRootData *TasksRoot::data() {
  return detachedData<TasksRootData>();
}

const TasksRootData *TasksRoot::data() const {
  return specializedData<TasksRootData>();
}

QList<PfNode> TasksRoot::originalPfNodes() const {
  auto d = data();
  return d ? d->_originalPfNodes : QList<PfNode>{};
}

PfNode TasksRoot::toPfNode() const {
  auto d = data();
  return d ? d->toPfNode() : PfNode();
}

void TasksRootData::fillPfNode(PfNode &node) const {
  // params and vars
  ConfigUtils::writeParamSet(&node, _params, "param");
  ConfigUtils::writeParamSet(&node, _vars, "var");
  ConfigUtils::writeParamSet(&node, _instanceparams, "instanceparam");

  // event subcription
  ConfigUtils::writeEventSubscriptions(&node, _onplan);
  ConfigUtils::writeEventSubscriptions(&node, _onstart);
  ConfigUtils::writeEventSubscriptions(&node, _onsuccess);
  ConfigUtils::writeEventSubscriptions(&node, _onfailure,
                                       excludeOnfinishSubscriptions);
  ConfigUtils::writeEventSubscriptions(&node, _onstderr);
  ConfigUtils::writeEventSubscriptions(&node, _onstdout);
  ConfigUtils::writeEventSubscriptions(&node, _onnostderr);
}

PfNode TasksRootData::toPfNode() const {
  PfNode node("tasksroot");
  TasksRootData::fillPfNode(node);
  return node;
}

bool TasksRoot::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
}

bool TasksRootData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  return SharedUiItemData::setUiData(section, value, errorString, transaction,
                                     role);
}

Qt::ItemFlags TasksRootData::uiFlags(int section) const {
  Qt::ItemFlags flags = SharedUiItemData::uiFlags(section);
  return flags;
}

const Utf8StringIndexedConstList TasksRootData::_sectionNames = {
  "tasklocalid", // 0
  "parent_group",
  "label",
  "mean",
  "command",
  "target", // 5
  "triggers",
  "parameters",
  "resources",
  "", // was: last_execution
  "next_execution", // 10
  "taskid",
  "max_instances",
  "running_count",
  "on_start",
  "on_success", // 15
  "on_failure",
  "running_slash_max",
  "actions",
  "", // was: last_execution_status
  "applied_templates", // was: System environment // 20
  "vars",
  "instance_params", // was: Unsetenv
  "min_expected_duration",
  "max_expected_duration",
  "overridable_params", // 25
  "", // was: last_execution_duration
  "max_duration_before_abort",
  "triggers_incl_calendars",
  "enabled",
  "has_triggers_with_calendars", // 30
  "herding_policy", // was: Workflow task
  "", // was: last_taskinstanceid
  "human_readable_info",
  "executions_count",
  "max_queued_instances", // 35 was: Enqueue policy
  "on_plan",
  "deduplicate_criterion",
  "merge_stdout_into_stderr",
  "on_stderr",
  "on_stdout", // 40
  "status_command",
  "abort_command",
  "max_tries",
  "pause_between_tries",
  "deduplicate_strategy", // 45
};

const Utf8StringIndexedConstList TasksRootData::_headerNames = {
  "Task local Id", // 0
  "Parent Group",
  "Label",
  "Mean",
  "Command",
  "Target", // 5
  "Triggers",
  "Parameters",
  "Resources",
  "", // was: Last execution
  "Next execution", // 10
  "Id",
  "Max instances",
  "Running count",
  "On start",
  "On success", // 15
  "On failure",
  "Running / max",
  "Actions",
  "On nostderr", // was: Last execution status
  "Applied templates", // was: System environment // 20
  "Vars",
  "Instance params", // was: Unsetenv
  "Min expected duration",
  "Max expected duration",
  "Overridable params", // 25
  "", // was: Last execution duration
  "Max duration before abort",
  "Triggers incl. calendars",
  "Enabled",
  "Has triggers with calendars", // 30
  "Herding policy", // was: Workflow task
  "", // was: Last taskinstanceid
  "Human readable info",
  "Executions count",
  "Max queued instances", // 35 was: Enqueue policy
  "On plan",
  "Deduplicate criterion",
  "Merge stdout into stderr",
  "On stderr",
  "On stdout", // 40
  "Status command",
  "Abort command",
  "Max tries",
  "Pause between tries",
  "Deduplicate strategy", // 45
};
