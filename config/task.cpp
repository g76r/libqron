/* Copyright 2012-2021 Hallowyn and others.
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
#include "task_p.h"
#include "taskgroup.h"
#include "log/log.h"
#include <QAtomicInt>
#include <QPointer>
#include "sched/scheduler.h"
#include "format/stringutils.h"
#include "action/action.h"
#include "ui/graphvizdiagramsbuilder.h"
#include "modelview/shareduiitemdocumentmanager.h"
#include "util/radixtree.h"
#include <functional>
#include "util/containerutils.h"

class TaskData : public TaskOrTemplateData {
public:
  QString _localId;
  TaskGroup _group;
  SharedUiItemList<TaskTemplate> _appliedTemplates;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _lastExecution, _nextScheduledExecution;
  // LATER QAtomicInt is not needed since only one thread changes these values (Executor's)
  mutable QAtomicInt _runningCount, _executionsCount;
  mutable bool _lastSuccessful;
  mutable int _lastReturnCode, _lastTotalMillis;
  mutable quint64 _lastTaskInstanceId;

  TaskData(): _lastExecution(LLONG_MIN), _nextScheduledExecution(LLONG_MIN),
      _lastSuccessful(true), _lastReturnCode(-1),
      _lastTotalMillis(-1), _lastTaskInstanceId(0) { }
  QDateTime lastExecution() const;
  QDateTime nextScheduledExecution() const;
  QVariant uiData(int section, int role) const override;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction,
                 int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  QString idQualifier() const override { return "task"; }
  PfNode toPfNode() const;
  void setTaskGroup(TaskGroup taskGroup);
};

Task::Task() {
}

Task::Task(const Task &other) : SharedUiItem(other) {
}

Task::Task(
    PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
    QHash<QString,Calendar> namedCalendars,
    QHash<QString, TaskTemplate> taskTemplates) {
  TaskData *d = new TaskData;
  d->_localId =
      ConfigUtils::sanitizeId(node.contentAsString(), ConfigUtils::LocalId);
  d->_id = taskGroup.id()+"."+d->_localId;
  d->_group = taskGroup;
  for (auto child: node.childrenByName("apply")) {
    for (auto name: child.contentAsStringList()) {
      auto tmpl = taskTemplates.value(name);
      if (tmpl.isNull()) {
        Log::warning() << "tasktemplate" << name << "not found while requested "
                          "in task definition: " << node.toString();
        continue;
      }
      if (!d->loadConfig(tmpl.data()->_originalPfNode, scheduler, taskGroup,
                         namedCalendars)) { // should never happen
        delete d;
        return;
      }
      d->_appliedTemplates.append(tmpl);
    }
  }
  if (!d->loadConfig(node, scheduler, taskGroup, namedCalendars)) {
    delete d;
    return;
  }
  // default mean: local
  if (d->_mean == UnknownMean)
    d->_mean = Local;
  // silently use "localhost" as target for targetless means
  if (d->_target.isEmpty())
    switch(d->_mean) {
    case Local:
    case DoNothing:
    case Docker:
      d->_target = "localhost";
      break;
    case UnknownMean: // impossible
    case Ssh:
    case Http:
      ;
    }
  setData(d);
}

void Task::copyLiveAttributesFromOldTask(Task oldTask) {
  TaskData *d = this->data();
  if (!d || oldTask.isNull())
    return;
  // copy mutable fields from old task (excepted _nextScheduledExecution)
  d->_lastExecution = oldTask.lastExecution().isValid()
      ? oldTask.lastExecution().toMSecsSinceEpoch() : LLONG_MIN;
  d->_runningCount = oldTask.runningCount();
  d->_executionsCount = oldTask.executionsCount();
  d->_lastSuccessful = oldTask.lastSuccessful();
  d->_lastReturnCode = oldTask.lastReturnCode();
  d->_lastTotalMillis = oldTask.lastTotalMillis();
  d->_lastTaskInstanceId = oldTask.lastTaskInstanceId();
  d->_enabled = oldTask.enabled();
  // keep last triggered timestamp from previously defined trigger
  QHash<QString,CronTrigger> oldCronTriggers;
  for (auto trigger: oldTask.data()->_cronTriggers)
    oldCronTriggers.insert(trigger.canonicalExpression(), trigger);
  for (auto trigger: d->_cronTriggers) {
    CronTrigger oldTrigger =
        oldCronTriggers.value(trigger.canonicalExpression());
    if (oldTrigger.isValid())
      trigger.setLastTriggered(oldTrigger.lastTriggered());
  }
}

Task Task::dummyTask() {
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
  TaskData *d = data();
  if (d)
    d->setTaskGroup(taskGroup);
}

void TaskData::setTaskGroup(TaskGroup taskGroup) {
  _group = taskGroup;
  _id = _group.id()+"."+_localId;
}

QHash<QString, qint64> Task::resources() const {
  return !isNull() ? data()->_resources : QHash<QString,qint64>();
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

int Task::runningCount() const {
  return !isNull() ? data()->_runningCount.loadRelaxed() : 0;
}

int Task::fetchAndAddRunningCount(int valueToAdd) const {
  return !isNull() ? data()->_runningCount.fetchAndAddOrdered(valueToAdd) : 0;
}

int Task::executionsCount() const {
  return !isNull() ? data()->_executionsCount.loadRelaxed() : 0;
}

int Task::fetchAndAddExecutionsCount(int valueToAdd) const {
  return !isNull() ? data()->_executionsCount.fetchAndAddOrdered(valueToAdd)
                   : 0;
}

QList<QRegularExpression> Task::stderrFilters() const {
  return !isNull() ? data()->_stderrFilters : QList<QRegularExpression>();
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

quint64 Task::lastTaskInstanceId() const {
  return !isNull() ? data()->_lastTaskInstanceId : 0;
}

void Task::setLastTaskInstanceId(quint64 lastTaskInstanceId) const {
  if (!isNull())
    data()->_lastTaskInstanceId = lastTaskInstanceId;
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

ParamSet Task::vars() const {
  return !isNull() ? data()->_vars : ParamSet();
}

Task::EnqueuePolicy Task::enqueuePolicy() const {
  return !isNull() ? data()->_enqueuePolicy : Task::EnqueuePolicyUnknown;
}

static QHash<Task::EnqueuePolicy,QString> _enqueuePolicyAsString {
  { Task::EnqueueAll, "enqueueall" },
  { Task::EnqueueAndDiscardQueued, "enqueueanddiscardqueued" },
  { Task::EnqueueUntilMaxInstances, "enqueueuntilmaxinstances" }
};

static QHash<QString,Task::EnqueuePolicy> _enqueuePolicyFromString {
  ContainerUtils::reversed(_enqueuePolicyAsString)
};

QString Task::enqueuePolicyAsString(Task::EnqueuePolicy v) {
  return _enqueuePolicyAsString.value(v, QStringLiteral("unknown"));
}

Task::EnqueuePolicy Task::enqueuePolicyFromString(QString v) {
  return _enqueuePolicyFromString.value(v, Task::EnqueuePolicyUnknown);
}

static QHash<Task::HerdingPolicy,QString> _herdingPolicyAsString {
  { Task::AllSuccess, "allsuccess" },
  { Task::NoFailure, "nofailure" },
  { Task::OneSuccess, "onesuccess" },
  { Task::OwnStatus, "ownstatus" },
  { Task::NoWait, "nowait" },
};

static QHash<QString,Task::HerdingPolicy> _herdingPolicyFromString {
  ContainerUtils::reversed(_herdingPolicyAsString)
};

Task::HerdingPolicy Task::herdingPolicy() const {
  auto d = data();
  return d ? d->_herdingPolicy : Task::HerdingPolicyUnknown;
}

QString Task::herdingPolicyAsString(Task::HerdingPolicy v) {
  return _herdingPolicyAsString.value(v, QStringLiteral("unknown"));
}

Task::HerdingPolicy Task::herdingPolicyFromString(QString v) {
  return _herdingPolicyFromString.value(v, Task::HerdingPolicyUnknown);
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

static RadixTree<std::function<QVariant(const Task&, const QVariant &)>> _pseudoParams {
{ "!tasklocalid" , [](const Task &task, const QVariant &) {
  return task.localId();
} },
{ "!taskid" , [](const Task &task, const QVariant &) {
  return task.id();
} },
{ "!taskgroupid" , [](const Task &task, const QVariant &) {
  return task.taskGroup().id();
} },
{ "!target" , [](const Task &task, const QVariant &) {
  return task.target();
} },
{ "!minexpectedms" , [](const Task &task, const QVariant &) {
  return task.minExpectedDuration();
} },
{ "!minexpecteds" , [](const Task &task, const QVariant &) {
  return (double)task.minExpectedDuration()/1000.0;
} },
{ "!maxexpectedms" , [](const Task &task, const QVariant &defaultValue) {
  long long ms = task.maxExpectedDuration();
  return (ms == LLONG_MAX) ? defaultValue : ms;
} },
{ "!maxexpecteds" , [](const Task &task, const QVariant &defaultValue) {
  long long ms = task.maxExpectedDuration();
  return (ms == LLONG_MAX) ? defaultValue : (double)ms/1000.0;
} },
{ "!maxbeforeabortms" , [](const Task &task, const QVariant &defaultValue) {
  long long ms = task.maxDurationBeforeAbort();
  return (ms == LLONG_MAX) ? defaultValue : ms;
} },
{ "!maxbeforeaborts", [](const Task &task, const QVariant &defaultValue) {
  long long ms = task.maxDurationBeforeAbort();
  return (ms == LLONG_MAX) ? defaultValue : (double)ms/1000.0;
} },
{ "!maxexpectedms0", [](const Task &task, const QVariant &) {
  long long ms = task.maxExpectedDuration();
  return (ms == LLONG_MAX) ? 0 : ms;
} },
{ "!maxexpecteds0", [](const Task &task, const QVariant &) {
  long long ms = task.maxExpectedDuration();
  return (ms == LLONG_MAX) ? 0.0 : (double)ms/1000.0;
} },
{ "!maxbeforeabortms0", [](const Task &task, const QVariant &) {
  long long ms = task.maxDurationBeforeAbort();
  return (ms == LLONG_MAX) ? 0 : ms;
} },
{ "!maxbeforeaborts0", [](const Task &task, const QVariant &) {
  long long ms = task.maxDurationBeforeAbort();
  return (ms == LLONG_MAX) ? 0.0 : (double)ms/1000.0;
} },
{ "", [](const Task &, const QVariant &defaultValue) {
  return defaultValue;
}, true },
};

QVariant TaskPseudoParamsProvider::paramValue(
    QString key, const ParamsProvider *context, QVariant defaultValue, QSet<QString>) const {
  Q_UNUSED(context)
  // the following is fail-safe thanks to the catch-all prefix in the radix tree
  return _pseudoParams.value(key)(_task, defaultValue);
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

SharedUiItemList<TaskTemplate> Task::appliedTemplates() const {
  auto d = data();
  return d ? d->_appliedTemplates : SharedUiItemList<TaskTemplate>();
}

QVariant TaskData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _localId;
    case 11:
      return _id;
    case 1:
      return _group.id();
    case 2:
      if (role == Qt::EditRole)
        return _label == _localId ? QVariant() : _label;
      return _label.isEmpty() ? _localId : _label;
    case 9:
      return lastExecution().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 10:
      return nextScheduledExecution().toString(
            QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"));
    case 13:
      return _runningCount.loadRelaxed();
    case 17:
      return QString::number(_runningCount.loadRelaxed())+" / "
          +QString::number(_maxInstances);
    case 19: {
      QDateTime dt = lastExecution();
      if (dt.isNull())
        return QVariant();
      QString returnCode = QString::number(_lastReturnCode);
      QString returnCodeLabel =
          _params.value("return.code."+returnCode+".label");
      QString s = dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"))
          .append(_lastSuccessful ? " success" : " failure")
          .append(" (code ").append(returnCode);
      if (!returnCodeLabel.isEmpty())
        s.append(" : ").append(returnCodeLabel);
      return s.append(')');
    }
    case 20:
      return _appliedTemplates.join(' ');
    case 26:
      return _lastTotalMillis >= 0 ? _lastTotalMillis/1000.0 : QVariant();
    case 32:
      return _lastTaskInstanceId > 0 ? _lastTaskInstanceId : QVariant();
    case 34:
      return _executionsCount.loadRelaxed();
    }
    break;
  default:
    ;
  }
  return TaskOrTemplateData::uiData(section, role);
}

bool Task::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
}

bool TaskData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  QString s = value.toString().trimmed(), s2;
  switch(section) {
  case 0:
    s = ConfigUtils::sanitizeId(s, ConfigUtils::LocalId);
    s2 = _group.id()+"."+s;
    _localId = s;
    _id = s2;
    return true;
  case 1: {
    s = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId);
    SharedUiItem group = transaction->itemById("taskgroup", s);
    if (group.isNull()) {
      *errorString = "No group with such id: \""+s+"\"";
      return false;
    }
    setTaskGroup(static_cast<TaskGroup&>(group));
    return true;
  }
  case 2:
    _label = value.toString().trimmed();
    if (_label == _localId)
      _label = QString();
    return true;
  }
  return TaskOrTemplateData::setUiData(
        section, value, errorString, transaction, role);
}

Qt::ItemFlags TaskData::uiFlags(int section) const {
  Qt::ItemFlags flags = TaskOrTemplateData::uiFlags(section);
  switch (section) {
  case 0:
  case 1:
  case 2:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

void Task::setParentParams(ParamSet parentParams) {
  if (!isNull())
    data()->_params.setParent(parentParams);
}

TaskData *Task::data() {
  return detachedData<TaskData>();
}

PfNode Task::originalPfNode() const {
  const TaskData *d = data();
  if (!d)
    return PfNode();
  return d->_originalPfNode;
}

PfNode Task::toPfNode() const {
  const TaskData *d = data();
  return d ? d->toPfNode() : PfNode();
}

PfNode TaskData::toPfNode() const {
  PfNode node("task", _localId);

  // comments
  ConfigUtils::writeComments(&node, _commentsList);

  // description and execution attributes
  node.setAttribute("taskgroup", _group.id());
  if (!_label.isEmpty() && _label != _localId)
    node.setAttribute("label", _label);
  if (!_info.isEmpty())
    node.setAttribute("info", _info);
  node.setAttribute("mean", Task::meanAsString(_mean));
  // do not set target attribute if it is empty,
  // or in case it is implicit ("localhost" for targetless means)
  if (!_target.isEmpty()
      && (_target != "localhost" ||
          (_mean != Task::Local && _mean != Task::DoNothing &&
           _mean != Task::Docker)))
    node.setAttribute("target", _target);
  // do not set command attribute if it is empty
  // or for means that do not use it (DoNothing)
  if (!_command.isEmpty()
      && _mean != Task::DoNothing) {
    // LATER store _command as QStringList _commandArgs instead, to make model consistent rather than splitting the \ escaping policy between here, uiData() and executor.cpp
    // moreover this is not consistent between means (luckily there are no backslashes nor spaces in http uris)
    QString escaped = _command;
    escaped.replace('\\', "\\\\");
    node.setAttribute("command", escaped);
  }

  // triggering and constraints attributes
  PfNode triggers("trigger");
  foreach (const Trigger &ct, _cronTriggers)
    triggers.appendChild(ct.toPfNode());
  foreach (const Trigger &nt, _noticeTriggers)
    triggers.appendChild(nt.toPfNode());
  node.appendChild(triggers);
  if (_enqueuePolicy != Task::EnqueueAndDiscardQueued)
    node.appendChild(
          PfNode("enqueuepolicy",
                 Task::enqueuePolicyAsString(_enqueuePolicy)));
  if (_maxInstances != 1)
    node.appendChild(PfNode("maxinstances",
                            QString::number(_maxInstances)));
  foreach (const QString &key, _resources.keys())
    node.appendChild(
          PfNode("resource",
                 key+" "+QString::number(_resources.value(key))));

  // params and vars
  ConfigUtils::writeParamSet(&node, _params, "param");
  ConfigUtils::writeParamSet(&node, _vars, "var");

  // monitoring and alerting attributes
  if (_maxExpectedDuration < LLONG_MAX)
    node.appendChild(PfNode("maxexpectedduration",
                            QString::number((double)_maxExpectedDuration/1e3)));
  if (_minExpectedDuration > 0)
    node.appendChild(PfNode("minexpectedduration",
                            QString::number((double)_minExpectedDuration/1e3)));
  if (_maxDurationBeforeAbort < LLONG_MAX)
    node.appendChild(PfNode("maxdurationbeforeabort",
                            QString::number((double)_maxDurationBeforeAbort
                                            /1e3)));

  // events
  ConfigUtils::writeEventSubscriptions(&node, _onstart);
  ConfigUtils::writeEventSubscriptions(&node, _onsuccess);
  ConfigUtils::writeEventSubscriptions(&node, _onfailure,
                                       excludeOnfinishSubscriptions);

  // user interface attributes
  if (!_requestFormFields.isEmpty()) {
    PfNode requestForm("requestform");
    foreach (const RequestFormField &field, _requestFormFields)
      requestForm.appendChild(field.toPfNode());
    node.appendChild(requestForm);
  }
  return node;
}

static QHash<Task::Mean,QString> _meansAsString {
  { Task::DoNothing, "donothing" },
  { Task::Local, "local" },
  { Task::Ssh, "ssh" },
  { Task::Docker, "docker" },
  { Task::Http, "http" },
};

static QHash<QString,Task::Mean> _meansFromString {
  ContainerUtils::reversed(_meansAsString)
};

static QStringList _validMeans {
  _meansFromString.keys()
};

Task::Mean Task::meanFromString(QString mean) {
  return _meansFromString.value(mean, UnknownMean);
}

QString Task::meanAsString(Task::Mean mean) {
  return _meansAsString.value(mean, QString());
}

QStringList Task::validMeanStrings() {
  return _validMeans;
}
