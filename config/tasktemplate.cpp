/* Copyright 2021-2023 Gregoire Barbier and others.
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
    PfNode node, Scheduler *scheduler, SharedUiItem parent,
    QHash<QString,Calendar> namedCalendars) {
  TaskTemplateData *d = new TaskTemplateData;
  d->_id =
      ConfigUtils::sanitizeId(node.contentAsString(), ConfigUtils::LocalId);
  if (!d->TaskOrTemplateData::loadConfig(node, scheduler, parent,
                                         namedCalendars)) {
    delete d;
    return;
  }
  setData(d);
}

bool TaskOrTemplateData::loadConfig(
    PfNode node, Scheduler *scheduler, SharedUiItem parent,
    QHash<QString,Calendar> namedCalendars) {
  if (parent.idQualifier() != "tasksroot"
      && parent.idQualifier() != "taskgroup") [[unlikely]] {
    Log::error() << "internal error in TaskOrGroupData::loadConfig";
    return false;
  }
  auto root = static_cast<const TasksRoot&>(parent);
  _mergeStderrIntoStdout = root.mergeStderrIntoStdout();
  if (!TaskOrGroupData::loadConfig(node, parent, scheduler))
    return false;
  if (!ConfigUtils::loadAttribute<Task::Mean>(
        node, "mean", &_mean,
        [](QString value) { return Task::meanFromString(value.trimmed()); },
        [](Task::Mean mean) { return mean != Task::UnknownMean; })) {
    Log::error() << idQualifier()+" with invalid execution mean: "
                 << node.toString();
    return false;
  }
  ConfigUtils::loadAttribute(node, "command", &_command);
  ConfigUtils::loadAttribute(node, "abortcommand", &_abortcommand);
  ConfigUtils::loadAttribute(node, "statuscommand", &_statuscommand);
  ConfigUtils::loadAttribute<QString>(
        node, "target", &_target, [](QString value) {
    return ConfigUtils::sanitizeId(value, ConfigUtils::FullyQualifiedId);
  });
  ConfigUtils::loadAttribute<QString>(
        node, "info", &_info,
        [info=_info](QString value) {
    return info + ((!info.isEmpty() && !value.isEmpty()) ? " " : "") + value;
  });
  if (!ConfigUtils::loadAttribute<int>(
        node, "maxinstances", &_maxInstances,
        [](QString value) { return value.toInt(0,0); },
        [](int value) { return value > 0; })) {
    Log::error() << "invalid "+idQualifier()+" maxinstances: " << node.toPf();
    return false;
  }
  if (!ConfigUtils::loadAttribute<int>(
        node, "maxtries", &_maxTries,
        [](QString value) { return value.toInt(0,0); },
        [](int value) { return value > 0; })) {
    Log::error() << "invalid "+idQualifier()+" maxtries: " << node.toPf();
    return false;
  }
  ConfigUtils::loadAttribute<long long>(
    node, "pausebetweentries", &_millisBetweenTries,
    [](QString value) {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "maxexpectedduration", &_maxExpectedDuration,
        [](QString value) {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "minexpectedduration", &_minExpectedDuration,
        [](QString value) {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  ConfigUtils::loadAttribute<long long>(
        node, "maxdurationbeforeabort", &_maxDurationBeforeAbort,
        [](QString value) {
      bool ok; double f = value.toDouble(&ok);
      return ok ? (long long)(std::max(f,0.0)*1000) : 0.0;
    });
  foreach (PfNode child, node.childrenByName("trigger")) {
    foreach (PfNode grandchild, child.children()) {
      QList<PfNode> inheritedComments;
      foreach (PfNode commentNode, child.children())
        if (commentNode.isComment())
          inheritedComments.append(commentNode);
      std::reverse(inheritedComments.begin(), inheritedComments.end());
      foreach (PfNode commentNode, inheritedComments)
        grandchild.prependChild(commentNode);
      QString content = grandchild.contentAsString();
      QString triggerType = grandchild.name();
      if (triggerType == "notice") {
        NoticeTrigger trigger(grandchild, namedCalendars);
        if (trigger.isValid()) {
          _noticeTriggers.append(trigger);
          Log::debug() << "configured notice trigger '" << content
                       << "' on "+idQualifier()+" '" << _id << "'";
        } else {
          Log::error() << idQualifier()+" with invalid notice trigger: "
                       << node.toString();
          return false;
        }
      } else if (triggerType == "cron") {
        CronTrigger trigger(grandchild, namedCalendars);
        if (trigger.isValid()) {
          _cronTriggers.append(trigger);
          Log::debug() << "configured cron trigger "
                       << trigger.humanReadableExpression()
                       << " on "+idQualifier()+" " << _id;
        } else {
          Log::error() << idQualifier()+" with invalid cron trigger: "
                       << grandchild.toString();
          return false;
        }
        // LATER read misfire config
      } else {
        Log::warning() << "ignoring unknown trigger type '" << triggerType
                       << "' on "+idQualifier()+" " << _id;
      }
    }
  }
  ConfigUtils::loadResourcesSet(node, &_resources, "resource");
  ConfigUtils::loadAttribute(node, "maxqueuedinstances", &_maxQueuedInstances);
  ConfigUtils::loadAttribute(node, "deduplicatecriterion",
                             &_deduplicateCriterion);
  _deduplicateCriterion.remove("!deduplicatecriterion"); // prevent recursivity
  if (!ConfigUtils::loadAttribute<Task::HerdingPolicy>(
        node, "herdingpolicy", &_herdingPolicy,
        [](QString value) { return Task::herdingPolicyFromString(value); },
        [](Task::HerdingPolicy p) { return p != Task::HerdingPolicyUnknown;})) {
    Log::error() << "invalid herdingpolicy on "+idQualifier()+": "
                 << node.toPf();
    return false;
  }
  QList<PfNode> children = node.childrenByName("requestform");
  if (!children.isEmpty()) {
    if (children.size() > 1) {
      Log::error() << idQualifier()+" with several requestform: "
                   << node.toString();
      return false;
    }
    for (auto child: children.last().childrenByName("field")) {
      RequestFormField field(child);
      if (!field.isNull())
        _requestFormFields.append(field);
    }
  }
  return true;
}

TaskTemplateData *TaskTemplate::data() {
  return detachedData<TaskTemplateData>();
}

PfNode TaskTemplate::originalPfNode() const {
  const TaskTemplateData *d = data();
  if (!d)
    return PfNode();
  return d->_originalPfNode;
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
    node.setAttribute("label", _label);

  return node;
}

QString TaskOrTemplateData::triggersAsString() const {
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

QString TaskOrTemplateData::triggersWithCalendarsAsString() const {
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

bool TaskOrTemplateData::triggersHaveCalendar() const {
  foreach (CronTrigger t, _cronTriggers)
    if (!t.calendar().isNull())
      return true;
  foreach (NoticeTrigger t, _noticeTriggers)
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
      foreach (const RequestFormField rff, _requestFormFields)
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
  QString s = value.toString().trimmed(), s2;
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
    QHash<QString,qint64> resources;
    if (QronUiUtils::resourcesFromString(value.toString(), &resources,
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
  // comments
  ConfigUtils::writeComments(&node, _commentsList);

  // description and execution attributes
  if (!_info.isEmpty())
    node.setAttribute("info", _info);
  node.setAttribute("mean", Task::meanAsString(_mean));
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
        node.setAttribute("target", _target);
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
    node.setAttribute("command", escaped);
  }
  if (!_statuscommand.isEmpty())
    node.setAttribute("statuscommand", _statuscommand);
  if (!_abortcommand.isEmpty())
    node.setAttribute("statuscommand", _abortcommand);

  // triggering and constraints attributes
  PfNode triggers("trigger");
  foreach (const Trigger &ct, _cronTriggers)
    triggers.appendChild(ct.toPfNode());
  foreach (const Trigger &nt, _noticeTriggers)
    triggers.appendChild(nt.toPfNode());
  node.appendChild(triggers);
  if (_maxQueuedInstances != "%!maxinstances")
    node.setAttribute("maxqueuedinstances", _maxQueuedInstances);
  if (!_deduplicateCriterion.isEmpty())
    node.setAttribute("deduplicatecriterion", _deduplicateCriterion);
  if (_herdingPolicy != Task::AllSuccess)
    node.appendChild(
          PfNode("herdingpolicy",
                 Task::herdingPolicyAsString(_herdingPolicy)));
  if (_maxInstances != 1)
    node.appendChild(PfNode("maxinstances",
                            QString::number(_maxInstances)));
  if (_maxTries != 1)
    node.appendChild(PfNode("maxtries",
                            QString::number(_maxTries)));
  if (_millisBetweenTries != 0)
    node.appendChild(PfNode("pausebetweentries",
                            QString::number(_millisBetweenTries*.001)));
  foreach (const QString &key, _resources.keys())
    node.appendChild(
          PfNode("resource",
                 key+" "+QString::number(_resources.value(key))));

  // params vars and event subscriptions
  TaskOrGroupData::fillPfNode(node);

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

  // user interface attributes
  if (!_requestFormFields.isEmpty()) {
    PfNode requestForm("requestform");
    foreach (const RequestFormField &field, _requestFormFields)
      requestForm.appendChild(field.toPfNode());
    node.appendChild(requestForm);
  }
}
