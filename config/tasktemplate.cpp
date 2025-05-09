/* Copyright 2021-2025 Gregoire Barbier and others.
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
#include "tasktemplate.h"
#include "task_p.h"
#include "tasksroot.h"

TaskTemplate::TaskTemplate() {
}

TaskTemplate::TaskTemplate(const TaskTemplate&other) : SharedUiItem(other) {
}

TaskTemplate::TaskTemplate(
    const PfNode &node, Scheduler *scheduler, const SharedUiItem &parent,
    const QMap<Utf8String, Calendar> &namedCalendars) {
  TaskTemplateData *d = new TaskTemplateData;
  d->_id = ConfigUtils::sanitizeId(node.content_as_text(),
                                   ConfigUtils::LocalId).toUtf8();
  if (!d->TaskOrTemplateData::loadConfig(node, scheduler, parent,
                                         namedCalendars)) {
    delete d;
    return;
  }
  setData(d);
}

bool TaskOrTemplateData::loadConfig(
    const PfNode &node, Scheduler *scheduler, const SharedUiItem &parent,
    const QMap<Utf8String,Calendar> &namedCalendars) {
  if (parent.qualifier() != "tasksroot"
      && parent.qualifier() != "taskgroup") [[unlikely]] {
    Log::error() << "internal error in TaskOrGroupData::loadConfig";
    return false;
  }
  auto root = static_cast<const TasksRoot&>(parent);
  _mergeStdoutIntoStderr = root.mergeStdoutIntoStderr();
  if (!TaskOrGroupData::loadConfig(node, parent, scheduler))
    return false;
  if (!ConfigUtils::loadAttribute<Task::Mean>(
        node, "mean", &_mean,
        [](QString value) STATIC_LAMBDA { return Task::meanFromString(value.trimmed()); },
        [](Task::Mean mean) STATIC_LAMBDA { return mean != Task::UnknownMean; })) {
    Log::error() << qualifier()+" with invalid execution mean: "
                 << node.as_text();
    return false;
  }
  ConfigUtils::loadAttribute(node, "command", &_command);
  ConfigUtils::loadAttribute(node, "abortcommand", &_abortcommand);
  ConfigUtils::loadAttribute(node, "statuscommand", &_statuscommand);
  ConfigUtils::loadAttribute<QString>(
        node, "target", &_target, [](QString value) STATIC_LAMBDA {
    return ConfigUtils::sanitizeId(value, ConfigUtils::FullyQualifiedId);
  });
  ConfigUtils::loadAttribute<QString>(
        node, "info", &_info,
        [info=_info](QString value) {
    return info + ((!info.isEmpty() && !value.isEmpty()) ? " " : "") + value;
  });
  if (!ConfigUtils::loadAttribute<int>(
        node, "maxinstances", &_maxInstances,
        [](QString value) STATIC_LAMBDA { return value.toInt(0,0); },
        [](int value) STATIC_LAMBDA { return value > 0; })) {
    Log::error() << "invalid "+qualifier()+" maxinstances: " << node.as_pf();
    return false;
  }
  if (!ConfigUtils::loadAttribute<int>(
        node, "maxtries", &_maxTries,
        [](QString value) STATIC_LAMBDA { return value.toInt(0,0); },
        [](int value) STATIC_LAMBDA { return value > 0; })) {
    Log::error() << "invalid "+qualifier()+" maxtries: " << node.as_pf();
    return false;
  }
  ConfigUtils::loadAttribute<long long>(
    node, "pausebetweentries", &_millisBetweenTries,
    [](QString value) STATIC_LAMBDA {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "maxexpectedduration", &_maxExpectedDuration,
        [](QString value) STATIC_LAMBDA {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "minexpectedduration", &_minExpectedDuration,
        [](QString value) STATIC_LAMBDA {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "maxdurationbeforeabort", &_maxDurationBeforeAbort,
        [](QString value) STATIC_LAMBDA {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  for (const PfNode &intermediate_node: node/"trigger") {
    for (const PfNode &trigger_node: intermediate_node.children()) {
      if (trigger_node^"notice") {
        NoticeTrigger trigger(trigger_node, namedCalendars);
        if (trigger.isValid()) {
          _noticeTriggers.append(trigger);
          Log::debug() << "configured notice trigger '"
                       << trigger_node.content_as_text()
                       << "' on "+qualifier()+" '" << _id << "'";
        } else {
          Log::error() << qualifier()+" with invalid notice trigger: "
                       << trigger_node.as_text();
          return false;
        }
      } else if (trigger_node^"cron") {
        CronTrigger trigger(trigger_node, namedCalendars);
        if (trigger.isValid()) {
          _cronTriggers.append(trigger);
          Log::debug() << "configured cron trigger "
                       << trigger.humanReadableExpression()
                       << " on "+qualifier()+" " << _id;
        } else {
          Log::error() << qualifier()+" with invalid cron trigger: "
                       << trigger_node.as_text();
          return false;
        }
        // LATER read misfire config
      } else {
        Log::warning() << "ignoring unknown trigger type '"
                       << trigger_node.name() << "' on "+qualifier()+" " << _id;
      }
    }
  }
  ConfigUtils::loadResourcesSet(node, &_resources, "resource");
  if (!ConfigUtils::loadAttribute<int>(
        node, "maxperhost", &_maxPerHost,
        [](QString value) STATIC_LAMBDA { return value.toInt(0,0); },
        [](int value) STATIC_LAMBDA { return value > 0; })) {
    Log::error() << "invalid "+qualifier()+" maxperhost: " << node.as_pf();
    return false;
  }
  if (_maxPerHost > 0)
    _resources["<maxperhost>"+_id] = 1;
  ConfigUtils::loadAttribute(node, "maxqueuedinstances", &_maxQueuedInstances);
  ConfigUtils::loadAttribute(node, "deduplicatecriterion",
                             &_deduplicateCriterion);
  _deduplicateCriterion.remove("!deduplicatecriterion"); // prevent recursivity
  ConfigUtils::loadAttribute(node, "deduplicatestrategy",
                             &_deduplicateStrategy);
  _deduplicateStrategy.remove("!deduplicatestrategy"); // prevent recursivity
  if (!ConfigUtils::loadAttribute<Task::HerdingPolicy>(
        node, "herdingpolicy", &_herdingPolicy,
        [](QString value) STATIC_LAMBDA { return Task::herdingPolicyFromString(value); },
        [](Task::HerdingPolicy p) STATIC_LAMBDA { return p != Task::HerdingPolicyUnknown;})) {
    Log::error() << "invalid herdingpolicy on "+qualifier()+": "
                 << node.as_pf();
    return false;
  }
  auto [child,unwanted] = node.first_two_children("requestform");
  if (!!child) {
    for (const auto &child: child/"field") {
      RequestFormField field(child);
      if (!!field)
        _requestFormFields.insert(field.id(), field);
    }
  }
  if (!!unwanted) {
    Log::error() << qualifier()+" with several requestform: " << node.as_text();
    return false;
  }
  return true;
}

TaskTemplateData *TaskTemplate::data() {
  return detachedData<TaskTemplateData>();
}

const TaskTemplateData *TaskTemplate::data() const {
  return specializedData<TaskTemplateData>();
}

QList<PfNode> TaskTemplate::originalPfNodes() const {
  const TaskTemplateData *d = data();
  if (!d)
    return QList<PfNode>{};
  return d->_originalPfNodes;
}

PfNode TaskTemplate::toPfNode() const {
  const TaskTemplateData *d = data();
  return d ? d->toPfNode() : PfNode();
}

bool TaskTemplate::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
}

PfNode TaskTemplateData::toPfNode() const {
  PfNode node("tasktemplate", _id);
  TaskOrTemplateData::fillPfNode(node);

  // description and execution attributes
  if (!_label.isEmpty() && _label != _id)
    node.set_attribute("label", _label);

  return node;
}

QString TaskOrTemplateData::triggersAsString() const {
  QString s;
  for (const CronTrigger &t: _cronTriggers)
    s.append(t.humanReadableExpression()).append(' ');
  for (const NoticeTrigger &t: _noticeTriggers)
    s.append(t.humanReadableExpression()).append(' ');
  for (const QString &t: _otherTriggers)
    s.append(t).append(' ');
  s.chop(1); // remove last space
  return s;
}

QString TaskOrTemplateData::triggersWithCalendarsAsString() const {
  QString s;
  for (const CronTrigger &t: _cronTriggers)
    s.append(t.humanReadableExpressionWithCalendar()).append(' ');
  for (const NoticeTrigger &t: _noticeTriggers)
    s.append(t.humanReadableExpressionWithCalendar()).append(' ');
  for (const QString &t: _otherTriggers)
    s.append(t).append(" ");
  s.chop(1); // remove last space
  return s;
}

bool TaskOrTemplateData::triggersHaveCalendar() const {
  for (const CronTrigger &t: _cronTriggers)
    if (!t.calendar().isNull())
      return true;
  for (const NoticeTrigger &t: _noticeTriggers)
    if (!t.calendar().isNull())
      return true;
  return false;
}

QVariant TaskOrTemplateData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 3:
      return Task::meanAsString(_mean);
    case 4: {
      // TODO this behavior is probably buggy/out of date
      QString escaped = _command;
      escaped.replace('\\', "\\\\");
      return escaped;
    }
    case 5:
      return _target;
    case 6:
      return triggersAsString();
    case 8:
      return QronUiUtils::resourcesAsString(_resources);
    case 12:
      return _maxInstances;
    case 23:
      return (_minExpectedDuration > 0)
          ? _minExpectedDuration*.001 : QVariant();
    case 24:
      return (_maxExpectedDuration < LLONG_MAX)
          ? _maxExpectedDuration*.001 : QVariant();
    case 25: {
      QString s;
      for (const RequestFormField &rff: _requestFormFields)
        s.append(rff.id()).append(' ');
      s.chop(1);
      return s;
    }
    case 27:
      return (_maxDurationBeforeAbort < LLONG_MAX)
          ? _maxDurationBeforeAbort*.001 : QVariant();
    case 28:
      return triggersWithCalendarsAsString();
    case 29:
      return _enabled;
    case 30:
      return triggersHaveCalendar();
    case 31:
      return Task::herdingPolicyAsString(_herdingPolicy);
    case 33:
      return _info;
    case 35:
      return _maxQueuedInstances;
    case 37:
      return _deduplicateCriterion;
    case 41:
      return _statuscommand;
    case 42:
      return _abortcommand;
    case 43:
      return _maxTries;
    case 44:
      return _millisBetweenTries*.001;
    case 45:
      return _deduplicateStrategy;
    }
    break;
  default:
    ;
  }
  return TaskOrGroupData::uiData(section, role);
}

bool TaskOrTemplateData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  //auto s = Utf8String(value).trimmed();
  switch(section) {
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
    _target = ConfigUtils::sanitizeId(
          value.toString(), ConfigUtils::FullyQualifiedId);
    return true;
  case 8: {
    QMap<Utf8String,qint64> resources;
    if (QronUiUtils::resourcesFromString(Utf8String(value), &resources,
                                         errorString)) {
      _resources = resources;
      return true;
    }
    return false;
  }
  }
  return TaskOrGroupData::setUiData(
        section, value, errorString, transaction, role);
}

Qt::ItemFlags TaskOrTemplateData::uiFlags(int section) const {
  Qt::ItemFlags flags = TaskOrGroupData::uiFlags(section);
  switch (section) {
  case 3:
  case 5:
  case 8:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

void TaskOrTemplateData::fillPfNode(PfNode &node) const {
  TaskOrGroupData::fillPfNode(node);

  // description and execution attributes
  if (!_info.isEmpty())
    node.set_attribute("info", _info);
  node.set_attribute("mean", Task::meanAsString(_mean));
  // do not set target attribute if it is empty,
  // or in case it is implicit ("localhost" for targetless means)
  if (!_target.isEmpty()) {
    switch(_mean) {
    case Task::Local:
    case Task::Background:
    case Task::DoNothing:
    case Task::Docker:
    case Task::Scatter:
      break;
    case Task::UnknownMean: [[unlikely]] // should never happen
    case Task::Ssh:
    case Task::Http:
      if (_target != "localhost")
        node.set_attribute("target", _target);
    }
  }
  // do not set command attribute if it is empty
  // or for means that do not use it (DoNothing)
  if (!_command.isEmpty()
      && _mean != Task::DoNothing) {
    // LATER store _command as QStringList _commandArgs instead, to make model consistent rather than splitting the \ escaping policy between here, uiData() and executor.cpp
    // moreover this is not consistent between means (luckily there are no backslashes nor spaces in http uris)
    QString escaped = _command;
    escaped.replace('\\', "\\\\");
    node.set_attribute("command", escaped);
  }
  if (!_statuscommand.isEmpty())
    node.set_attribute("statuscommand", _statuscommand);
  if (!_abortcommand.isEmpty())
    node.set_attribute("statuscommand", _abortcommand);

  // triggering and constraints attributes
  PfNode triggers("trigger");
  for (const Trigger &ct: _cronTriggers)
    triggers.append_child(ct.toPfNode());
  for (const Trigger &nt: _noticeTriggers)
    triggers.append_child(nt.toPfNode());
  node.append_child(triggers);
  if (_maxQueuedInstances != "%!maxinstances")
    node.set_attribute("maxqueuedinstances", _maxQueuedInstances);
  if (!_deduplicateCriterion.isEmpty())
    node.set_attribute("deduplicatecriterion", _deduplicateCriterion);
  if (!_deduplicateStrategy.isEmpty())
    node.set_attribute("deduplicatestrategy", _deduplicateStrategy);
  if (_herdingPolicy != Task::AllSuccess)
    node.append_child(
          PfNode("herdingpolicy",
                 Task::herdingPolicyAsString(_herdingPolicy)));
  if (_maxInstances != 1)
    node.append_child(PfNode("maxinstances",
                            QString::number(_maxInstances)));
  if (_maxPerHost > 0)
    node.append_child(PfNode("maxperhost",
                            QString::number(_maxPerHost)));
  if (_maxTries != 1)
    node.append_child(PfNode("maxtries",
                            QString::number(_maxTries)));
  if (_millisBetweenTries != 0)
    node.append_child(PfNode("pausebetweentries",
                            QString::number(_millisBetweenTries*.001)));
  for (const auto &[k,v]: _resources.asKeyValueRange())
    if (!k.startsWith("<maxperhost>"))
      node.append_child(PfNode("resource", k+" "+QString::number(v)));

  // params vars and event subscriptions
  TaskOrGroupData::fillPfNode(node);

  // monitoring and alerting attributes
  if (_maxExpectedDuration < LLONG_MAX)
    node.append_child(PfNode("maxexpectedduration",
                            QString::number((double)_maxExpectedDuration/1e3)));
  if (_minExpectedDuration > 0)
    node.append_child(PfNode("minexpectedduration",
                            QString::number((double)_minExpectedDuration/1e3)));
  if (_maxDurationBeforeAbort < LLONG_MAX)
    node.append_child(PfNode("maxdurationbeforeabort",
                            QString::number((double)_maxDurationBeforeAbort
                                            /1e3)));

  // user interface attributes
  if (!_requestFormFields.isEmpty()) {
    PfNode requestForm("requestform");
    for (const RequestFormField &field: _requestFormFields)
      requestForm.append_child(field.toPfNode());
    node.append_child(requestForm);
  }
}
