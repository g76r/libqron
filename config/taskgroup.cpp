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
#include "taskgroup.h"
#include "util/paramset.h"
#include <QString>
#include "task.h"
#include "pf/pfnode.h"
#include <QtDebug>
#include "config/eventsubscription.h"
#include <QPointer>
#include "config/configutils.h"
#include "sched/taskinstance.h"
#include "task_p.h"
#include "ui/qronuiutils.h"
#include "modelview/shareduiitemdocumentmanager.h"

static const QSet<QString> excludedDescendantsForComments {
  "onsuccess", "onfailure", "onfinish", "onstart", "ontrigger"
};

static const QStringList excludeOnfinishSubscriptions { "onfinish" };

class TaskGroupData : public SharedUiItemData {
public:
  QString _id, _label;
  ParamSet _params, _vars;
  QList<EventSubscription> _onstart, _onsuccess, _onfailure;
  QStringList _commentsList;
  QVariant uiData(int section, int role) const;
  QVariant uiHeaderData(int section, int role) const;
  int uiSectionCount() const;
  QString id() const { return _id; }
  //void setId(QString id) { _id = id; }
  QString idQualifier() const { return "taskgroup"; }
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  Qt::ItemFlags uiFlags(int section) const;

};

TaskGroup::TaskGroup() {
}

TaskGroup::TaskGroup(const TaskGroup &other) : SharedUiItem(other) {
}

TaskGroup::TaskGroup(ParamSet params, ParamSet vars) {
  TaskGroupData *d = new TaskGroupData;
  d->_params = params;
  d->_vars = vars;
  setData(d);
}

TaskGroup::TaskGroup(PfNode node, TaskGroup parentGroup, Scheduler *scheduler) {
  TaskGroupData *d = new TaskGroupData;
  d->_id = ConfigUtils::sanitizeId(node.contentAsString(),
                                     ConfigUtils::FullyQualifiedId);
  d->_label = node.attribute("label");
  d->_params.setParent(parentGroup.params());
  ConfigUtils::loadParamSet(node, &d->_params, "param");
  d->_vars.setParent(parentGroup.vars());
  ConfigUtils::loadParamSet(node, &d->_vars, "var");
  d->_onstart.append(parentGroup.onstartEventSubscriptions());
  ConfigUtils::loadEventSubscription(node, "onstart", d->_id,
                                     &d->_onstart, scheduler);
  d->_onsuccess.append(parentGroup.onsuccessEventSubscriptions());
  ConfigUtils::loadEventSubscription(node, "onsuccess", d->_id,
                                     &d->_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(node, "onfinish", d->_id,
                                     &d->_onsuccess, scheduler);
  d->_onfailure.append(parentGroup.onfailureEventSubscriptions());
  ConfigUtils::loadEventSubscription(node, "onfailure", d->_id,
                                     &d->_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(node, "onfinish", d->_id,
                                     &d->_onfailure, scheduler);
  ConfigUtils::loadComments(node, &d->_commentsList,
                            excludedDescendantsForComments);
  setData(d);
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

void TaskGroup::triggerStartEvents(TaskInstance instance) const {
  // LATER trigger events in parent group first
  if (isNull())
    return;
  for (const auto &sub: data()->_onstart)
    sub.triggerActions(instance);
}

void TaskGroup::triggerSuccessEvents(TaskInstance instance) const {
  if (isNull())
    return;
  for (auto sub: data()->_onsuccess)
    sub.triggerActions(instance);
}

void TaskGroup::triggerFailureEvents(TaskInstance instance) const {
  if (isNull())
    return;
  for (auto sub: data()->_onfailure)
    sub.triggerActions(instance);
}

QList<EventSubscription> TaskGroup::onstartEventSubscriptions() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onsuccessEventSubscriptions() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> TaskGroup::onfailureEventSubscriptions() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

ParamSet TaskGroup::vars() const {
  return !isNull() ? data()->_vars : ParamSet();
}

QList<EventSubscription> TaskGroup::allEventSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onstart + data()->_onsuccess + data()->_onfailure
                   : QList<EventSubscription>();
}

QVariant TaskGroupData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
    case 11:
      return _id;
    case 1:
      return TaskGroup::parentGroupId(_id);
    case 2:
      if (role == Qt::EditRole)
        return _label == _id ? QVariant() : _label;
      return _label.isEmpty() ? _id : _label;
    case 7:
      return _params.toString(false, false);
    case 14:
      return EventSubscription::toStringList(_onstart).join("\n");
    case 15:
      return EventSubscription::toStringList(_onsuccess).join("\n");
    case 16:
      return EventSubscription::toStringList(_onfailure).join("\n");
    case 20:
      return QVariant(); // was: System environment
    case 21:
      return QronUiUtils::paramsAsString(_vars);
    case 22:
      return QVariant(); // was: Unsetenv
    }
    break;
  default:
    ;
  }
  return QVariant();
}

QVariant TaskGroupData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int TaskGroupData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

TaskGroupData *TaskGroup::data() {
  return detachedData<TaskGroupData>();
}

PfNode TaskGroup::toPfNode() const {
  const TaskGroupData *d = data();
  if (!d)
    return PfNode();
  PfNode node(QStringLiteral("taskgroup"), d->id());
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_label.isNull())
    node.setAttribute(QStringLiteral("label"), d->_label);
  ConfigUtils::writeParamSet(&node, d->_params, QStringLiteral("param"));
  ConfigUtils::writeParamSet(&node, d->_vars, QStringLiteral("var"));
  ConfigUtils::writeEventSubscriptions(&node, d->_onstart);
  ConfigUtils::writeEventSubscriptions(&node, d->_onsuccess);
  ConfigUtils::writeEventSubscriptions(&node, d->_onfailure,
                                       excludeOnfinishSubscriptions);
  return node;
}

bool TaskGroup::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
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
    Q_FALLTHROUGH();
  case 0:
  case 11:
    s = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId);
    _id = s;
    return true;
  case 2:
    _label = value.toString().trimmed();
    if (_label == _id)
      _label = QString();
    return true;
  }
  return SharedUiItemData::setUiData(section, value, errorString, transaction,
                                     role);
}

Qt::ItemFlags TaskGroupData::uiFlags(int section) const {
  Qt::ItemFlags flags = SharedUiItemData::uiFlags(section);
  switch (section) {
  case 0:
  case 1:
  case 2:
  case 11:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}
