/* Copyright 2014-2023 Hallowyn and others.
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
#ifndef TASK_P_H
#define TASK_P_H

#include "task.h"
#include "configutils.h"
#include "trigger/noticetrigger.h"
#include "trigger/crontrigger.h"
#include "eventsubscription.h"
#include "requestformfield.h"
#include "ui/qronuiutils.h"
#include "modelview/templatedshareduiitemdata.h"

static QSet<QString> excludedDescendantsForComments {
  "trigger", "onsuccess", "onfailure", "onfinish", "onstart", "onplan",
  "onstderr", "onstdout"
};

static QStringList excludeOnfinishSubscriptions { "onfinish" };

#define TASKSROOTID "*"

class TasksRootData
    : public SharedUiItemDataWithImmutableParams<TasksRootData> {
public:
  static const Utf8String _idQualifier;
  static const Utf8StringList _sectionNames;
  static const Utf8StringList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  Utf8String _id;
  ParamSet _vars, _instanceparams;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure, _onplan, _onstderr,
      _onstdout;
  QStringList _commentsList;
  PfNode _originalPfNode;
  bool _mergeStderrIntoStdout = false;

  TasksRootData(const Utf8String &id = TASKSROOTID) : _id(id) {}
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  bool loadConfig(PfNode node, Scheduler *scheduler);
  void fillPfNode(PfNode &node) const;
};

class TaskOrGroupData : public TasksRootData {
public:
  Utf8String _label;

  QVariant uiData(int section, int role) const override;
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  bool loadConfig(PfNode node, SharedUiItem parentGroup, Scheduler *scheduler);
  void fillPfNode(PfNode &node) const;
};

class TaskOrTemplateData : public TaskOrGroupData {
public:
  QString _command, _statuscommand, _abortcommand, _target, _info;
  Task::Mean _mean;
  QList<NoticeTrigger> _noticeTriggers;
  QMap<QString,qint64> _resources;
  int _maxInstances, _maxTries;
  long long _millisBetweenTries;
  QList<CronTrigger> _cronTriggers;
  long long _maxExpectedDuration, _minExpectedDuration, _maxDurationBeforeAbort;
  QString _maxQueuedInstances, _deduplicateCriterion, _deduplicateStrategy;
  Task::HerdingPolicy _herdingPolicy;
  QList<RequestFormField> _requestFormFields;
  QStringList _otherTriggers; // guessed indirect triggers resulting from events
  mutable bool _enabled;

  TaskOrTemplateData() : _maxInstances(1), _maxTries(1), _millisBetweenTries(0),
    _maxExpectedDuration(LLONG_MAX),
    _minExpectedDuration(0), _maxDurationBeforeAbort(LLONG_MAX),
    _maxQueuedInstances("%!maxinstances"),
    _herdingPolicy(Task::AllSuccess), _enabled(true) { }
  QString triggersAsString() const;
  QString triggersWithCalendarsAsString() const;
  bool triggersHaveCalendar() const;
  QVariant uiData(int section, int role) const override;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction,
                 int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  void setId(QByteArray id) { _id = id; }
  bool loadConfig(PfNode node, Scheduler *scheduler, SharedUiItem parent,
                  QMap<QByteArray, Calendar> namedCalendars);
  void fillPfNode(PfNode &node) const;
};

class TaskTemplateData : public TaskOrTemplateData {
public:
  Utf8String idQualifier() const override { return "tasktemplate"_u8; }
  PfNode toPfNode() const;
};

const Utf8String TasksRootData::_idQualifier = "tasksroot";

const Utf8StringList TasksRootData::_sectionNames = {
  "tasklocalid", // 0
  "parent_group",
  "label",
  "mean",
  "command",
  "target", // 5
  "triggers",
  "parameters",
  "resources",
  "last_execution",
  "next_execution", // 10
  "taskid",
  "max_instances",
  "running_count",
  "on_start",
  "on_success", // 15
  "on_failure",
  "running_slash_max",
  "actions",
  "last_execution_status",
  "applied_templates", // was: System environment // 20
  "vars",
  "instance_params", // was: Unsetenv
  "min_expected_duration",
  "max_expected_duration",
  "overridable_params", // 25
  "last_execution_duration",
  "max_duration_before_abort",
  "triggers_incl_calendars",
  "enabled",
  "has_triggers_with_calendars", // 30
  "herding_policy", // was: Workflow task
  "last_taskinstanceid",
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

const Utf8StringList TasksRootData::_headerNames = {
  "Task local Id", // 0
  "Parent Group",
  "Label",
  "Mean",
  "Command",
  "Target", // 5
  "Triggers",
  "Parameters",
  "Resources",
  "Last execution",
  "Next execution", // 10
  "Id",
  "Max instances",
  "Running count",
  "On start",
  "On success", // 15
  "On failure",
  "Running / max",
  "Actions",
  "Last execution status",
  "Applied templates", // was: System environment // 20
  "Vars",
  "Instance params", // was: Unsetenv
  "Min expected duration",
  "Max expected duration",
  "Overridable params", // 25
  "Last execution duration",
  "Max duration before abort",
  "Triggers incl. calendars",
  "Enabled",
  "Has triggers with calendars", // 30
  "Herding policy", // was: Workflow task
  "Last taskinstanceid",
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

#endif // TASK_P_H
