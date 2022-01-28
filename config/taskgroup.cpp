/* Copyright 2012-2022 Hallowyn and others.
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
#include <QtDebug>
#include <QPointer>
#include "sched/taskinstance.h"
#include "modelview/shareduiitemdocumentmanager.h"
#include "tasksroot.h"

class TaskGroupData : public TaskOrGroupData {
public:
  QVariant uiData(int section, int role) const;
  QString idQualifier() const { return "taskgroup"; }
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  Qt::ItemFlags uiFlags(int section) const;
  PfNode toPfNode() const;
  bool loadConfig(PfNode node, SharedUiItem parentGroup, Scheduler *scheduler);
};

TaskGroup::TaskGroup() {
}

TaskGroup::TaskGroup(const TaskGroup &other) : SharedUiItem(other) {
}

TaskGroup::TaskGroup(PfNode node, SharedUiItem parent, Scheduler *scheduler) {
  TaskGroupData *d = new TaskGroupData;
  d->_id = ConfigUtils::sanitizeId(node.contentAsString(),
                                   ConfigUtils::FullyQualifiedId);
  if (d->loadConfig(node, parent, scheduler))
    setData(d);
}

bool TaskGroupData::loadConfig(
    PfNode node, SharedUiItem parent, Scheduler *scheduler) {
  if (!TaskOrGroupData::loadConfig(node, parent, scheduler))
    return false;
  return true;
}

bool TaskOrGroupData::loadConfig(
    PfNode node, SharedUiItem parent, Scheduler *scheduler) {
  if (parent.idQualifier() != "tasksroot"
      && parent.idQualifier() != "taskgroup") {
    qWarning() << "internal error in TaskOrGroupData::loadConfig";
    return false;
  }
  auto root = static_cast<const TasksRoot&>(parent);
  ConfigUtils::loadAttribute(node, "label", &_label);
  _params.setParent(root.params());
  _vars.setParent(root.vars());
  _instanceparams.setParent(root.instanceparams());
  _mergeStderrIntoStdout = root.mergeStderrIntoStdout();
  if (!TasksRootData::loadConfig(node, scheduler))
    return false;
  return true;
}

TaskGroup::TaskGroup(QString id) {
  TaskGroupData *d = new TaskGroupData;
  d->_id = ConfigUtils::sanitizeId(id, ConfigUtils::FullyQualifiedId);
  setData(d);
}

QString TaskGroup::parentGroupId(QString groupId) {
  int i = groupId.lastIndexOf('.');
  return (i >= 0) ? groupId.left(i) : QString();
}

QString TaskGroup::label() const {
  return !isNull() ? (data()->_label.isNull() ? data()->_id : data()->_label)
                   : QString();
}

ParamSet TaskGroup::params() const {
  return !isNull() ? data()->_params : ParamSet();
}

QList<EventSubscription> TaskGroup::onplan() const {
  return !isNull() ? data()->_onplan : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onstart() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onsuccess() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onfailure() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onstderr() const {
  return !isNull() ? data()->_onstderr : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onstdout() const {
  return !isNull() ? data()->_onstdout : QList<EventSubscription>();
}

ParamSet TaskGroup::vars() const {
  return !isNull() ? data()->_vars : ParamSet();
}

ParamSet TaskGroup::instanceparams() const {
  return !isNull() ? data()->_instanceparams : ParamSet();
}

QList<EventSubscription> TaskGroup::allEventSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onplan + data()->_onstart + data()->_onsuccess
                         + data()->_onfailure
                         + data()->_onstderr + data()->_onstdout
                   : QList<EventSubscription>();
}

QVariant TaskOrGroupData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
    case 11:
      return _id;
    case 2:
      if (role == Qt::EditRole)
        return _label == _id ? QVariant() : _label;
      return _label.isEmpty() ? _id : _label;
    }
    break;
  default:
    ;
  }
  return TasksRootData::uiData(section, role);
}

QVariant TaskGroupData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 1:
      return TaskGroup::parentGroupId(_id);
    }
    break;
  default:
    ;
  }
  return TaskOrGroupData::uiData(section, role);
}

TaskGroupData *TaskGroup::data() {
  return detachedData<TaskGroupData>();
}

PfNode TaskGroup::originalPfNode() const {
  const TaskGroupData *d = data();
  if (!d)
    return PfNode();
  return d->_originalPfNode;
}

PfNode TaskGroup::toPfNode() const {
  const TaskGroupData *d = data();
  return d ? d->toPfNode() : PfNode();
}

void TaskOrGroupData::fillPfNode(PfNode &node) const {
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
}

PfNode TaskGroupData::toPfNode() const {
  PfNode node("taskgroup", _id);
  ConfigUtils::writeComments(&node, _commentsList);
  TaskOrGroupData::fillPfNode(node);
  return node;
}

bool TaskGroup::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
}

bool TaskOrGroupData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  QString s = value.toString().trimmed(), s2;
  switch(section) {
  case 0:
  case 11:
    _id = ConfigUtils::sanitizeId(s, ConfigUtils::LocalId);
    return true;
  case 2:
    _label = value.toString().trimmed();
    if (_label == _id)
      _label = QString();
    return true;
  }
  return TasksRootData::setUiData(
      section, value, errorString, transaction, role);
}

bool TaskGroupData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  QString s = value.toString().trimmed();
  switch(section) {
  case 1: // changing parent group id is changing the begining of id itself
    if (_id.contains('.'))
      s = s+_id.mid(_id.lastIndexOf('.'));
    else
      s = s+"."+_id;
    _id = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId);
    return true;
  }
  return TaskOrGroupData::setUiData(
        section, value, errorString, transaction, role);
}

Qt::ItemFlags TaskOrGroupData::uiFlags(int section) const {
  Qt::ItemFlags flags = TasksRootData::uiFlags(section);
  switch (section) {
  case 0:
  case 2:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

Qt::ItemFlags TaskGroupData::uiFlags(int section) const {
  Qt::ItemFlags flags = TaskOrGroupData::uiFlags(section);
  switch (section) {
  case 1:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}
