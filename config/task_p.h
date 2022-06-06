/* Copyright 2014-2022 Hallowyn and others.
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
#include "pf/pfnode.h"
#include "ui/qronuiutils.h"

static QString _uiHeaderNames[] = {
  "Local Id", // 0
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
  "Request form overridable params", // 25
  "Last execution duration",
  "Max duration before abort",
  "Triggers incl. calendars",
  "Enabled",
  "Has triggers with calendars", // 30
  "Herding policy", // was: Workflow task
  "Last task instance id",
  "Additional info",
  "Executions count",
  "Max queued instances", // 35 was: Enqueue policy
  "On plan",
  "Deduplicate criterion",
  "Merge stdout into stderr",
  "On stderr",
  "On stdout", // 40
  "Status command",
  "Abort command"
};

static QSet<QString> excludedDescendantsForComments {
  "trigger", "onsuccess", "onfailure", "onfinish", "onstart", "onplan",
  "onstderr", "onstdout"
};

static QStringList excludeOnfinishSubscriptions { "onfinish" };

#define TASKSROOTID "*"

class TasksRootData : public SharedUiItemData {
public:
  ParamSet _params, _vars, _instanceparams;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure, _onplan, _onstderr,
      _onstdout;
  QStringList _commentsList;
  PfNode _originalPfNode;
  bool _mergeStderrIntoStdout = false;

  QVariant uiData(int section, int role) const override;
  QVariant uiHeaderData(int section, int role) const override;
  int uiSectionCount() const override;
  QString idQualifier() const override { return "tasksroot"; }
  QString id() const override { return TASKSROOTID; }
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  bool loadConfig(PfNode node, Scheduler *scheduler);
  void fillPfNode(PfNode &node) const;
};

class TaskOrGroupData : public TasksRootData {
public:
  QString _id, _label;

  QVariant uiData(int section, int role) const override;
  QString idQualifier() const override = 0;
  QString id() const override { return _id; }
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
  QHash<QString,qint64> _resources;
  int _maxInstances;
  QList<CronTrigger> _cronTriggers;
  long long _maxExpectedDuration, _minExpectedDuration, _maxDurationBeforeAbort;
  QString _maxQueuedInstances, _deduplicateCriterion;
  Task::HerdingPolicy _herdingPolicy;
  QList<RequestFormField> _requestFormFields;
  QStringList _otherTriggers; // guessed indirect triggers resulting from events
  mutable bool _enabled;

  TaskOrTemplateData() : _maxInstances(1), _maxExpectedDuration(LLONG_MAX),
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
  void setId(QString id) { _id = id; }
  bool loadConfig(PfNode node, Scheduler *scheduler, SharedUiItem parent,
                  QHash<QString,Calendar> namedCalendars);
  void fillPfNode(PfNode &node) const;
};

class TaskTemplateData : public TaskOrTemplateData {
public:
  TaskTemplateData() { }
  QString idQualifier() const override { return "tasktemplate"; }
  PfNode toPfNode() const;
};

#endif // TASK_P_H
