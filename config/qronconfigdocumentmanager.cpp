/* Copyright 2015-2025 Hallowyn and others.
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
#include "qronconfigdocumentmanager.h"
#include "task.h"
#include "modelview/genericshareduiitem.h"

QronConfigDocumentManager::QronConfigDocumentManager(QObject *parent)
  : SharedUiItemDocumentManager(parent) {
  registerItemType(
        "taskgroup", &TaskGroup::setUiData,
        [this](QByteArray id) -> SharedUiItem {
    return TaskGroup(PfNode("taskgroup", id), _config.tasksRoot(), 0);
  });
  addChangeItemTrigger(
        "taskgroup", AfterUpdate,
        [](SharedUiItemDocumentTransaction *transaction,
        SharedUiItem *newItem, SharedUiItem oldItem,
        QByteArray qualifier, QString *errorString) STATIC_LAMBDA {
    Q_UNUSED(oldItem)
    Q_UNUSED(qualifier)
    TaskGroup *newGroup = static_cast<TaskGroup*>(newItem);
    if (newItem->id() != oldItem.id()) {
      for (const auto &sui:
           transaction->foreignKeySources("task", 1, oldItem.id())) {
        auto oldTask = sui.casted<const Task>();
        Task newTask = oldTask;
        newTask.setTaskGroup(*newGroup);
        if (!transaction->changeItem(newTask, oldTask, "task", errorString))
          return false;
      }
    }
    return true;
  });
  registerItemType(
        "task", &Task::setUiData,
        [](SharedUiItemDocumentTransaction *transaction, QByteArray id,
        QString *errorString) STATIC_LAMBDA -> SharedUiItem {
    Q_UNUSED(transaction)
    Q_UNUSED(id)
    *errorString = "Cannot create task outside GUI";
    return SharedUiItem();
  });
  addForeignKey("task", 1, "taskgroup", 0, NoAction, Cascade);
  registerItemType(
        "host", &Host::setUiData,
        [this](QByteArray id) -> SharedUiItem {
    return Host(PfNode("host", id), _config.params());
  });
  addChangeItemTrigger(
        "host", BeforeUpdate|BeforeCreate,
        [](SharedUiItemDocumentTransaction *transaction, SharedUiItem *newItem,
        SharedUiItem oldItem, QByteArray qualifier,
        QString *errorString) STATIC_LAMBDA {
    Q_UNUSED(oldItem)
    Q_UNUSED(qualifier)
    if (!transaction->itemById("cluster", newItem->id()).isNull()) {
      *errorString = "Host id already used by a cluster: "+newItem->id();
      return false;
    }
    return true;
  });
  addChangeItemTrigger(
        "host", AfterUpdate|AfterDelete,
        [](SharedUiItemDocumentTransaction *transaction, SharedUiItem *newItem,
        SharedUiItem oldItem, QByteArray qualifier,
        QString *errorString) STATIC_LAMBDA {
    Q_UNUSED(qualifier)
    // cannot be a fk because target can reference either a host or a cluster
    for (const SharedUiItem &oldTaskItem:
         transaction->foreignKeySources("task", 5, oldItem.id())) {
      const Task &oldTask = static_cast<const Task&>(oldTaskItem);
      Task newTask = oldTask;
      newTask.setTarget(newItem->id());
      if (!transaction->changeItem(newTask, oldTask, "task", errorString))
        return false;
    }
    // on host change, upgrade every cluster it belongs to
    for (const SharedUiItem &oldClusterItem:
         transaction->itemsByQualifier("cluster")) {
      auto &oldCluster = static_cast<const Cluster &>(oldClusterItem);
      auto hosts = oldCluster.hosts();
      for (int i = 0; i < hosts.size(); ++i) {
        if(hosts[i] == oldItem) {
          Cluster newCluster = oldCluster;
          if (newItem->isNull())
            hosts.removeAt(i);
          else
            hosts[i] = newItem->casted<Host>();
          newCluster.setHosts(hosts);
          if (!transaction->changeItem(newCluster, oldCluster, "cluster",
                                       errorString))
            return false;
          break;
        }
      }
    }
    return true;
  });
  registerItemType(
        "cluster", &Cluster::setUiData, [](QByteArray id) STATIC_LAMBDA -> SharedUiItem {
    return Cluster(PfNode("cluster", id));
  });
  addChangeItemTrigger(
        "cluster", BeforeUpdate|BeforeCreate,
        [](SharedUiItemDocumentTransaction *transaction, SharedUiItem *newItem,
        SharedUiItem oldItem, QByteArray qualifier,
        QString *errorString) STATIC_LAMBDA {
    Q_UNUSED(oldItem)
    Q_UNUSED(qualifier)
    if (!transaction->itemById("host", newItem->id()).isNull()) {
      *errorString = "Cluster id already used by a host: "+newItem->id();
      return false;
    }
    return true;
  });
  addChangeItemTrigger(
        "cluster", AfterUpdate|AfterDelete,
        [](SharedUiItemDocumentTransaction *transaction, SharedUiItem *newItem,
        SharedUiItem oldItem, QByteArray qualifier,
        QString *errorString) STATIC_LAMBDA {
    Q_UNUSED(oldItem)
    Q_UNUSED(qualifier)
    // cannot be a fk because target can reference either a host or a cluster
    for (const SharedUiItem &oldTaskItem:
         transaction->foreignKeySources("task", 5, oldItem.id())) {
      const Task &oldTask = static_cast<const Task&>(oldTaskItem);
      Task newTask = oldTask;
      newTask.setTarget(newItem->id());
      if (!transaction->changeItem(newTask, oldTask, "task", errorString))
        return false;
    }
    return true;
  });
  // TODO register other items kinds
}

SharedUiItem QronConfigDocumentManager::itemById(
    const Utf8String &qualifier, const Utf8String &id) const {
  // TODO also implement for other items
  if (qualifier == "task") {
    return _config.tasks().value(id);
  } else if (qualifier == "taskgroup") {
    return _config.taskgroups().value(id);
  } else if (qualifier == "host") {
    return _config.hosts().value(id);
  } else if (qualifier == "cluster") {
    return _config.clusters().value(id);
  }
  return SharedUiItem();
}

SharedUiItemList QronConfigDocumentManager::itemsByQualifier(
    const Utf8String &qualifier) const {
  // TODO also implement for other items
  if (qualifier == "task") {
    return _config.tasks().values();
  } else if (qualifier == "taskgroup") {
    return _config.taskgroups().values();
  } else if (qualifier == "host") {
    return _config.hosts().values();
  } else if (qualifier == "cluster") {
    return _config.clusters().values();
  } else if (qualifier == "calendar") {
    // LATER is it right to return only *named* calendars ?
    return _config.namedCalendars().values();
  }
  return {};
}

static const SharedUiItem _nullItem;

template<class T>
void inline QronConfigDocumentManager::emitSignalForItemTypeChanges(
    const QMap<Utf8String, T> &newItems, const QMap<Utf8String, T> &oldItems,
    const Utf8String &qualifier) {
  for (const T &oldItem: oldItems)
    if (!newItems.contains(oldItem.id()))
      emit itemChanged(_nullItem, oldItem, qualifier);
  for (const T &newItem: newItems)
    emit itemChanged(newItem, oldItems.value(newItem.id()), qualifier);
}

template<>
void inline QronConfigDocumentManager::emitSignalForItemTypeChanges<PfNode>(
    const QMap<Utf8String, PfNode> &newItems,
    const QMap<Utf8String, PfNode> &oldItems,
    const Utf8String &qualifier) {
  for (const auto &oldItem: oldItems) {
    auto name = oldItem.name();
    if (!newItems.contains(name))
      emit itemChanged(
          _nullItem, GenericSharedUiItem(qualifier, name), qualifier);
  }
  for (const auto &newItem: newItems) {
    auto name = newItem.name();
    auto item = GenericSharedUiItem(qualifier, name);
    emit itemChanged(
          item, oldItems.contains(name) ? item : _nullItem, qualifier);
  }
}

template<>
void inline QronConfigDocumentManager::emitSignalForItemTypeChanges<Task>(
    const QMap<Utf8String,Task> &newItems,
    const QMap<Utf8String,Task> &oldItems,
    const Utf8String &qualifier) {
  for (const Task &oldItem: oldItems) {
    if (!newItems.contains(oldItem.id())) {
      emit itemChanged(_nullItem, oldItem, qualifier);
    }
  }
  QList<Task> newList = newItems.values();
  std::sort(newList.begin(), newList.end());
  for (const Task &newItem: newList) {
    const Task &oldItem = oldItems.value(newItem.id());
    emit itemChanged(newItem, oldItem, qualifier);
  }
}

void QronConfigDocumentManager::setConfig(SchedulerConfig newConfig,
                                          QMutexLocker<QMutex> *locker) {
  SchedulerConfig oldConfig = _config;
  _config = newConfig;
  if (locker)
    locker->unlock();
  emit paramsChanged(newConfig.params(), oldConfig.params(), "globalparams"_u8);
  emit paramsChanged(newConfig.vars(), oldConfig.vars(), "globalvars"_u8);
  emit accessControlConfigurationChanged(
        !newConfig.accessControlConfig().isEmpty());
  emitSignalForItemTypeChanges(
        newConfig.hosts(), oldConfig.hosts(), "host"_u8);
  emitSignalForItemTypeChanges(
        newConfig.clusters(), oldConfig.clusters(), "cluster"_u8);
  emitSignalForItemTypeChanges(
        newConfig.namedCalendars(), oldConfig.namedCalendars(), "calendar"_u8);
  emitSignalForItemTypeChanges(
        newConfig.externalParams(), oldConfig.externalParams(),
        "externalparams"_u8);
  emitSignalForItemTypeChanges(
        newConfig.taskgroups(), oldConfig.taskgroups(), "taskgroup"_u8);
  emitSignalForItemTypeChanges(
        newConfig.tasks(), oldConfig.tasks(), "task"_u8);
  // TODO also implement for other items
  emit globalEventSubscriptionsChanged(
        newConfig.onstart(), newConfig.onsuccess(), newConfig.onfailure(),
        newConfig.onlog(), newConfig.onnotice(), newConfig.onschedulerstart(),
        newConfig.onconfigload());
}

bool QronConfigDocumentManager::prepareChangeItem(
    SharedUiItemDocumentTransaction *transaction, const SharedUiItem &new_item,
    const SharedUiItem &old_item, const Utf8String &qualifier,
    QString *errorString) {
  QByteArray oldId = old_item.id(), newId = new_item.id();
  QString reason;
  if (qualifier == "taskgroup") {
    // currently nothing to do
  } else if (qualifier == "task") {
    // currently nothing to do
  } else if (qualifier == "cluster") {
    // currently nothing to do
  } else if (qualifier == "host") {
    // currently nothing to do
  } else if (qualifier == "calendar") {
    // currently nothing to do
  } else {
    reason = "QronConfigDocumentManager::changeItem do not support item type: "
        +qualifier;
  }
  // TODO implement more item types
  if (reason.isNull()) {
    storeItemChange(transaction, new_item, old_item, qualifier);
    //qDebug() << "QronConfigDocumentManager::prepareChangeItem succeeded:"
    //         << qualifier << newItem.id() << oldId;
    return true;
  } else {
    if (errorString)
      *errorString = reason;
    return false;
  }
}

void QronConfigDocumentManager::commitChangeItem(
    const SharedUiItem &new_item, const SharedUiItem &old_item,
    const Utf8String &qualifier) {
  _config.changeItem(new_item, old_item, qualifier);
  //qDebug() << "QronConfigDocumentManager::commitChangeItem done"
  //         << newItem.qualifiedId() << oldItem.qualifiedId();
  SharedUiItemDocumentManager::commitChangeItem(new_item, old_item, qualifier);
}

void QronConfigDocumentManager::changeParams(
    const ParamSet &newParams, const ParamSet &oldParams,
    const Utf8String &setId) {
  Q_UNUSED(newParams)
  Q_UNUSED(oldParams)
  Q_UNUSED(setId)
  qWarning() << "QronConfigDocumentManager::changeParams no longer implemented";
  //_config.changeParams(newParams, oldParams, setId);
  //emit paramsChanged(newParams, oldParams, setId);
}
