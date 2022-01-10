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
  "Enqueue policy" // 35
};

static QSet<QString> excludedDescendantsForComments {
  "trigger", "onsuccess", "onfailure", "onfinish", "onstart"
};

static QStringList excludeOnfinishSubscriptions { "onfinish" };

class TaskOrGroupData : public SharedUiItemData {
public:
  QString _id, _label;
  ParamSet _params, _vars, _instanceparams;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure;
  QStringList _commentsList;
  PfNode _originalPfNode;

  QVariant uiData(int section, int role) const;
  QVariant uiHeaderData(int section, int role) const;
  int uiSectionCount() const;
  QString id() const { return _id; }
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  Qt::ItemFlags uiFlags(int section) const;
  bool loadConfig(PfNode node, TaskGroup parentGroup, Scheduler *scheduler);
  void fillPfNode(PfNode &node) const;
};

class TaskOrTemplateData : public TaskOrGroupData {
public:
  QString _command, _target, _info;
  Task::Mean _mean;
  QList<NoticeTrigger> _noticeTriggers;
  QHash<QString,qint64> _resources;
  int _maxInstances;
  QList<CronTrigger> _cronTriggers;
  QList<QRegularExpression> _stderrFilters;
  long long _maxExpectedDuration, _minExpectedDuration, _maxDurationBeforeAbort;
  Task::EnqueuePolicy _enqueuePolicy;
  Task::HerdingPolicy _herdingPolicy;
  QList<RequestFormField> _requestFormFields;
  QStringList _otherTriggers; // guessed indirect triggers resulting from events
  mutable bool _enabled;

  TaskOrTemplateData() : _maxInstances(1), _maxExpectedDuration(LLONG_MAX),
    _minExpectedDuration(0), _maxDurationBeforeAbort(LLONG_MAX),
    _enqueuePolicy(Task::EnqueueUntilMaxInstances),
    _herdingPolicy(Task::NoFailure), _enabled(true) { }
  QString triggersAsString() const;
  QString triggersWithCalendarsAsString() const;
  bool triggersHaveCalendar() const;
  QVariant uiData(int section, int role) const override;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction,
                 int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  void setId(QString id) { _id = id; }
  bool loadConfig(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
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
