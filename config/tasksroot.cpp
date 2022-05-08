/* Copyright 2022 Gregoire Barbier and others.
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
#include "tasksroot.h"
#include "task_p.h"
#include <QtDebug>

TasksRoot::TasksRoot() {
}

TasksRoot::TasksRoot(const TasksRoot &other) : SharedUiItem(other) {
}

TasksRoot::TasksRoot(PfNode node, Scheduler *scheduler) {
  TasksRootData *d = new TasksRootData;
  if (d->loadConfig(node, scheduler))
    setData(d);
}

bool TasksRootData::loadConfig(
    PfNode node, Scheduler *scheduler) {
  _originalPfNode = node;
  _params += ParamSet(node, "param");
  _vars += ParamSet(node, "var");
  _instanceparams += ParamSet(node, "instanceparam");
  ConfigUtils::loadBoolean(node, "mergestderrintostdout",
                           &_mergeStderrIntoStdout);
  ConfigUtils::loadEventSubscription(
      node, "onplan", TASKSROOTID, &_onplan, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstart", TASKSROOTID, &_onstart, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onsuccess", TASKSROOTID, &_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfinish", TASKSROOTID, &_onsuccess, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfailure", TASKSROOTID, &_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onfinish", TASKSROOTID, &_onfailure, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstderr", TASKSROOTID, &_onstderr, scheduler);
  ConfigUtils::loadEventSubscription(
      node, "onstdout", TASKSROOTID, &_onstdout, scheduler);
  ConfigUtils::loadComments(
      node, &_commentsList, excludedDescendantsForComments);
  return true;
}

ParamSet TasksRoot::params() const {
  return !isNull() ? data()->_params : ParamSet();
}

ParamSet TasksRoot::vars() const {
  return !isNull() ? data()->_vars : ParamSet();
}

ParamSet TasksRoot::instanceparams() const {
  return !isNull() ? data()->_instanceparams : ParamSet();
}

QList<EventSubscription> TasksRoot::onplan() const {
  return !isNull() ? data()->_onplan : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstart() const {
  return !isNull() ? data()->_onstart : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onsuccess() const {
  return !isNull() ? data()->_onsuccess : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onfailure() const {
  return !isNull() ? data()->_onfailure : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstderr() const {
  return !isNull() ? data()->_onstderr : QList<EventSubscription>();
}

QList<EventSubscription> TasksRoot::onstdout() const {
  return !isNull() ? data()->_onstdout : QList<EventSubscription>();
}


QList<EventSubscription> TasksRoot::allEventSubscriptions() const {
  // LATER avoid creating the collection at every call
  return !isNull() ? data()->_onplan + data()->_onstart + data()->_onsuccess
                         + data()->_onfailure
                         + data()->_onstderr + data()->_onstdout
                   : QList<EventSubscription>();
}

QVariant TasksRootData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
                 && (unsigned)section < sizeof _uiHeaderNames
             ? _uiHeaderNames[section] : QVariant();
}

bool TasksRoot::mergeStderrIntoStdout() const {
  return !isNull() ? data()->_mergeStderrIntoStdout : false;
}

int TasksRootData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

QVariant TasksRootData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 7:
      return _params.toString(false, false);
    case 14:
      return EventSubscription::toStringList(_onstart).join("\n");
    case 15:
      return EventSubscription::toStringList(_onsuccess).join("\n");
    case 16:
      return EventSubscription::toStringList(_onfailure).join("\n");
    case 21:
      return _vars.toString(false, false);
    case 22:
      return _instanceparams.toString(false, false);
    case 36:
      return EventSubscription::toStringList(_onplan).join("\n");
    case 38:
      return _mergeStderrIntoStdout;
    case 39:
      return EventSubscription::toStringList(_onstderr).join("\n");
    case 40:
      return EventSubscription::toStringList(_onstdout).join("\n");
    }
    break;
  default:
      ;
  }
  return QVariant();
}

TasksRootData *TasksRoot::data() {
  return detachedData<TasksRootData>();
}

bool TasksRoot::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return data()->setUiData(section, value, errorString, transaction, role);
}

bool TasksRootData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  return SharedUiItemData::setUiData(section, value, errorString, transaction,
                                     role);
}

Qt::ItemFlags TasksRootData::uiFlags(int section) const {
  Qt::ItemFlags flags = SharedUiItemData::uiFlags(section);
  return flags;
}
