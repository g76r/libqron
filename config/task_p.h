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
    : public SharedUiItemDataWithImmutableParams<TasksRootData,true> {
public:
  static const Utf8String _qualifier; // dummy b/c of subclassing
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  Utf8String _id;
  ParamSet _vars, _instanceparams;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure, _onplan, _onstderr,
      _onstdout;
  Utf8StringList _commentsList;
  PfNode _originalPfNode;
  bool _mergeStderrIntoStdout = false;

  TasksRootData(const Utf8String &id = TASKSROOTID) : _id(id) {
    _params.setScope(qualifier()); }
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  bool loadConfig(PfNode node, Scheduler *scheduler);
  void fillPfNode(PfNode &node) const;
  Utf8String qualifier() const override { return "tasksroot"_u8; }
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
  Utf8String qualifier() const override { return "taskorgroup"_u8; }
};

class TaskOrTemplateData : public TaskOrGroupData {
public:
  QString _command, _statuscommand, _abortcommand, _target, _info;
  Task::Mean _mean;
  QList<NoticeTrigger> _noticeTriggers;
  QMap<Utf8String,qint64> _resources;
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
  void setId(const Utf8String &id) { _id = id; }
  bool loadConfig(PfNode node, Scheduler *scheduler, SharedUiItem parent,
                  QMap<Utf8String, Calendar> namedCalendars);
  void fillPfNode(PfNode &node) const;
  Utf8String qualifier() const override { return "taskortemplate"_u8; }
};

class TaskTemplateData : public TaskOrTemplateData {
public:
  TaskTemplateData() { _params.setScope(qualifier()); }
  Utf8String qualifier() const override { return "tasktemplate"_u8; }
  PfNode toPfNode() const;
};

#endif // TASK_P_H
