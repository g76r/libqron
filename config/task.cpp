/* Copyright 2012-2024 Hallowyn and others.
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
#include "sched/scheduler.h"
#include "action/action.h"
#include "ui/graphvizdiagramsbuilder.h"

class TaskData : public TaskOrTemplateData {
public:
  QByteArray _localId;
  SharedUiItemList _appliedTemplates;
  // note: since QDateTime (as most Qt classes) is not thread-safe, it cannot
  // be used in a mutable QSharedData field as soon as the object embedding the
  // QSharedData is used by several thread at a time, hence the qint64
  mutable qint64 _lastExecution, _nextScheduledExecution;
  // LATER QAtomicInt is not needed since only one thread changes these values (Scheduler's)
  mutable QAtomicInt _runningCount, _executionsCount;
  mutable bool _lastSuccessful;
  mutable int _lastReturnCode, _lastDurationMillis;
  mutable quint64 _lastTaskInstanceId;

  TaskData(): _lastExecution(LLONG_MIN), _nextScheduledExecution(LLONG_MIN),
      _lastSuccessful(true), _lastReturnCode(-1),
      _lastDurationMillis(-1), _lastTaskInstanceId(0) {
    _params.setScope(qualifier());
  }
  QDateTime lastExecution() const;
  QDateTime nextScheduledExecution() const;
  QVariant uiData(int section, int role) const override;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction,
                 int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
  Utf8String qualifier() const override { return "task"_u8; }
  PfNode toPfNode() const;
  void fillPfNode(PfNode &node) const;
  void setTaskGroup(TaskGroup taskGroup);
};

Task::Task() {
}

Task::Task(const Task &other) : SharedUiItem(other) {
}

Task::Task(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
    QMap<Utf8String, Calendar> namedCalendars,
    QMap<Utf8String, TaskTemplate> taskTemplates) {
  TaskData *d = new TaskData;
  d->_localId = ConfigUtils::sanitizeId(node.contentAsUtf16(),
                                        ConfigUtils::LocalId).toUtf8();
  d->_id = taskGroup.id()+"."+d->_localId;
  d->_group = taskGroup;
  for (auto child: node/"apply") {
    for (auto name: child.contentAsStringList()) {
      auto tmpl = taskTemplates.value(name.toUtf8());
      if (tmpl.isNull()) {
        Log::warning() << "tasktemplate" << name << "not found while requested "
                          "in task definition: " << node.toString();
        continue;
      }
      auto tmpl_node = tmpl.data()->_originalPfNodes.value(0);
      if (!d->loadConfig(tmpl_node, scheduler, taskGroup, namedCalendars)) {
        delete d; // should never happen
        return;
      }
      d->_appliedTemplates.append(tmpl);
    }
  }
  if (!d->loadConfig(node, scheduler, taskGroup, namedCalendars)) {
    delete d;
    return;
  }
  d->_originalPfNodes.prepend(d->_originalPfNodes.takeLast());
  for (auto group = taskGroup; !!group; group = group.parentGroup())
    d->_originalPfNodes.append(group.originalPfNodes().value(0));
  d->_originalPfNodes.append(taskGroup.tasksRoot().toPfNode());
  // default mean: local
  if (d->_mean == UnknownMean)
    d->_mean = Local;
  // silently use "localhost" as target for targetless means
  if (d->_target.isEmpty())
    switch(d->_mean) {
    case Local:
    case Background:
    case DoNothing:
    case Docker:
    case Scatter:
      d->_target = "localhost";
      break;
    case UnknownMean: // impossible
    case Ssh:
    case Http:
      ;
    }
  setData(d);
}

void Task::copyLiveAttributesFromOldTask(const Task &oldTask) {
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
  d->_lastDurationMillis = oldTask.lastDurationMillis();
  d->_lastTaskInstanceId = oldTask.lastTaskInstanceId();
  d->_enabled = oldTask.enabled();
  // keep last triggered timestamp from previously defined trigger
  QMap<QByteArray,CronTrigger> oldCronTriggers;
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

QByteArray Task::localId() const {
  return !isNull() ? data()->_localId : QByteArray{};
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

QString Task::statuscommand() const {
  return !isNull() ? data()->_statuscommand : QString();
}

QString Task::abortcommand() const {
  return !isNull() ? data()->_abortcommand : QString();
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

QMap<Utf8String,qint64> Task::resources() const {
  return !isNull() ? data()->_resources : QMap<Utf8String,qint64>{};
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

int Task::maxPerHost() const {
  return !isNull() ? data()->_maxPerHost : 0;
}

int Task::maxTries() const {
  return !isNull() ? data()->_maxTries : 0;
}

int Task::millisBetweenTries() const {
  return !isNull() ? data()->_millisBetweenTries : 0;
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

QList<EventSubscription> Task::onplan() const {
  return !isNull() ? data()->_onplan : QList<EventSubscription>();
}

QList<EventSubscription> Task::onstart() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> Task::onsuccess() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> Task::onfailure() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

QList<EventSubscription> Task::onstderr() const {
  return !isNull() ? data()->_onstderr : QList<EventSubscription>();
}

QList<EventSubscription> Task::onstdout() const {
  return !isNull() ? data()->_onstdout : QList<EventSubscription>();
}

QList<EventSubscription> Task::allEventsSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onplan + data()->_onstart + data()->_onsuccess
                         + data()->_onfailure
                         + data()->_onstderr + data()->_onstdout
                   : QList<EventSubscription>();
}

bool Task::mergeStdoutIntoStderr() const {
  return !isNull() ? data()->_mergeStdoutIntoStderr : false;
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

int Task::lastDurationMillis() const {
  return !isNull() ? data()->_lastDurationMillis : -1;
}

void Task::setLastDurationMillis(int lastDurationMillis) const {
  if (!isNull())
    data()->_lastDurationMillis = lastDurationMillis;
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

ParamSet Task::instanceparams() const {
  return !isNull() ? data()->_instanceparams : ParamSet();
}

QString Task::maxQueuedInstances() const {
  auto d = data();
  return d ? d->_maxQueuedInstances : QString();
}

QString Task::deduplicateCriterion() const {
  auto d = data();
  return d ? d->_deduplicateCriterion : QString();
}

QString Task::deduplicateStrategy() const {
  auto d = data();
  return d ? d->_deduplicateStrategy : QString{};
}

static QMap<Task::HerdingPolicy,QString> _herdingPolicyToText {
  { Task::AllSuccess, "allsuccess" },
  { Task::NoFailure, "nofailure" },
  { Task::OneSuccess, "onesuccess" },
  { Task::OwnStatus, "ownstatus" },
  { Task::NoWait, "nowait" },
};

static RadixTree<Task::HerdingPolicy> _herdingPolicyFromText =
    RadixTree<Task::HerdingPolicy>::reversed(_herdingPolicyToText);

Task::HerdingPolicy Task::herdingPolicy() const {
  auto d = data();
  return d ? d->_herdingPolicy : Task::HerdingPolicyUnknown;
}

QString Task::herdingPolicyAsString(Task::HerdingPolicy v) {
  return _herdingPolicyToText.value(v, QStringLiteral("unknown"));
}

Task::HerdingPolicy Task::herdingPolicyFromString(QString v) {
  return _herdingPolicyFromText.value(v, Task::HerdingPolicyUnknown);
}

QMap<QString, RequestFormField> Task::requestFormFields() const {
  return !isNull() ? data()->_requestFormFields
                   : QMap<QString, RequestFormField>{};
}

QString Task::requestFormFieldsAsHtmlDescription() const {
  auto list = requestFormFields();
  if (list.isEmpty())
    return "(none)";
  QString v;
  for (const RequestFormField &rff: list)
    v.append(rff.toHtmlHumanReadableDescription());
  return v;
}

const SharedUiItemDataFunctions TasksRootData::_paramFunctions = {
  { "!tasklocalid", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_localId;
    } },
  { "!taskid", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TasksRootData*>(data);
      if (!td)
        return {};
      return td->_id;
    } },
  { "!taskgroupid", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_group.id();
    } },
  { "!target", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_target;
    } },
  { "!minexpectedms", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_minExpectedDuration;
    } },
  { "!minexpecteds", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_minExpectedDuration/1e3;
    } },
  { "!maxexpectedms", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxExpectedDuration;
      return (ms == LLONG_MAX) ? QVariant{} : QVariant(ms);
    } },
  { "!maxexpecteds", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxExpectedDuration;
      return (ms == LLONG_MAX) ? QVariant{} : QVariant(ms/1e3);
    } },
  { "!maxbeforeabortms", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxDurationBeforeAbort;
      return (ms == LLONG_MAX) ? QVariant{} : QVariant(ms);
    } },
  { "!maxbeforeaborts", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxDurationBeforeAbort;
      return (ms == LLONG_MAX) ? QVariant{} : QVariant(ms/1e3);
    } },
  { "!maxexpectedms0", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxExpectedDuration;
      return (ms == LLONG_MAX) ? QVariant(0) : QVariant(ms);
    } },
  { "!maxexpecteds0", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxExpectedDuration;
      return (ms == LLONG_MAX) ? QVariant(0.0) : QVariant(ms/1e3);
    } },
  { "!maxbeforeabortms0", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxDurationBeforeAbort;
      return (ms == LLONG_MAX) ? QVariant(0) : QVariant(ms);
    } },
  { "!maxbeforeaborts0", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      auto ms = td->_maxDurationBeforeAbort;
      return (ms == LLONG_MAX) ? QVariant(0.0) : QVariant(ms/1e3);
    } },
  { "!maxinstances", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_maxInstances;
    } },
  { "!maxtries", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_maxTries;
    } },
  { "!rawdeduplicatecriterion", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_deduplicateCriterion;
    } },
  { "!deduplicatestrategy", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto td = dynamic_cast<const TaskData*>(data);
      if (!td)
        return {};
      return td->_deduplicateStrategy;
    } },
};

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

SharedUiItemList Task::appliedTemplates() const {
  auto d = data();
  return d ? d->_appliedTemplates : SharedUiItemList{};
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
      return lastExecution().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
    case 10:
      return nextScheduledExecution().toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
    case 13:
      return _runningCount.loadRelaxed();
    case 17:
      return QByteArray::number(_runningCount.loadRelaxed())+" / "
          +QByteArray::number(_maxInstances);
    case 19: {
      QDateTime dt = lastExecution();
      if (dt.isNull())
        return {};
      auto returnCode = Utf8String::number(_lastReturnCode);
      auto returnCodeLabel = _params.paramUtf8("return.code."+returnCode
                                               +".label");
      Utf8String s = Utf8String(dt.toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s))
          + (_lastSuccessful ? " success"_u8 : " failure"_u8)
          +" (code "_u8 + returnCode;
      if (!returnCodeLabel.isEmpty())
        s = s + " : "_u8 + returnCodeLabel;
      s += ')';
      return s;
    }
    case 20:
      return _appliedTemplates.join(' ');
    case 26:
      return _lastDurationMillis >= 0 ? _lastDurationMillis/1000.0 : QVariant{};
    case 32:
      return _lastTaskInstanceId > 0 ? _lastTaskInstanceId : QVariant{};
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
  QByteArray s = value.toString().trimmed().toUtf8(), s2;
  switch(section) {
  case 0:
    s = ConfigUtils::sanitizeId(s, ConfigUtils::LocalId).toUtf8();
    s2 = _group.id()+"."+s;
    _localId = s;
    _id = s2;
    return true;
  case 1: {
    s = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId).toUtf8();
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
      _label = {};
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

const TaskData *Task::data() const {
  return specializedData<TaskData>();
}

QList<PfNode> Task::originalPfNodes() const {
  const TaskData *d = data();
  if (!d)
    return QList<PfNode>{};
  return d->_originalPfNodes;
}

PfNode Task::toPfNode() const {
  auto d = data();
  return d ? d->toPfNode() : PfNode();
}

void TaskData::fillPfNode(PfNode &node) const {
  TaskOrTemplateData::fillPfNode(node);

  // applied templates
  if (!_appliedTemplates.isEmpty())
    node.setAttribute("apply", _appliedTemplates.join(' '));
}

PfNode TaskData::toPfNode() const {
  PfNode node("task", _localId);
  TaskData::fillPfNode(node);
  return node;
}

static QMap<Task::Mean,QString> _meansAsString {
  { Task::DoNothing, "donothing" },
  { Task::Local, "local" },
  { Task::Background, "background" },
  { Task::Ssh, "ssh" },
  { Task::Docker, "docker" },
  { Task::Http, "http" },
  { Task::Scatter, "scatter" },
};

static QHash<QString,Task::Mean> _meansFromString {
  ContainerUtils::reversed_hash(_meansAsString)
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

void Task::detach() {
  detachedData<TaskData>();
}
