/* Copyright 2012-2023 Hallowyn and others.
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
#include "clustersmodel.h"
#include <QMimeData>

/** Host reference item, to be inserted as cluster child in cluster tree,
 * without the need of having real host items which data are inconsistent with
 * cluster data */
class HostReference : public SharedUiItem {
  class HostReferenceData : public SharedUiItemData {
  public:
    Utf8String _id;
    QString _cluster, _host;

    HostReferenceData() { }
    HostReferenceData(QString cluster, QString host)
      : _id((cluster+"~"+host).toUtf8()), _cluster(cluster), _host(host) { }
    Utf8String id() const override { return _id; }
    Utf8String qualifier() const override { return "hostreference"_u8; }
    int uiSectionCount() const override { return 1; }
    QVariant uiData(int section, int role) const override {
      return section == 0 && role == Qt::DisplayRole ? _host : QVariant{}; }
    QVariant uiHeaderData(int section, int role) const override {
      return section == 0 && role == Qt::DisplayRole
          ? "Host"_u8 : QVariant{}; }
    Utf8String uiSectionName(int section) const override {
      return section == 0 ? "host"_u8 : Utf8String{}; }
    int uiSectionByName(Utf8String sectionName) const override {
      return sectionName == "host"_u8 ? 0 : -1; }
  };

public:
  HostReference() : SharedUiItem() { }
  explicit HostReference(QString cluster, QString host)
    : SharedUiItem(new HostReferenceData(cluster, host)) { }
  HostReference(const HostReference &other)
    : SharedUiItem(other) { }
  HostReference &operator=(const HostReference &other) {
    SharedUiItem::operator=(other); return *this; }
  QString cluster() const { return isNull() ? QString() : data()->_cluster; }

private:
  const HostReferenceData *data() const {
    return reinterpret_cast<const HostReferenceData*>(SharedUiItem::data()); }
};

Q_DECLARE_TYPEINFO(HostReference, Q_MOVABLE_TYPE);

ClustersModel::ClustersModel(QObject *parent)
  : SharedUiItemsTreeModel(parent) {
  setHeaderDataFromTemplate(Cluster(PfNode("template")));
}

void ClustersModel::changeItem(
    const SharedUiItem &newItem, const SharedUiItem &oldItem,
    const Utf8String &qualifier) {
  if (qualifier == "cluster"_u8) {
    // remove host references rows
    QModelIndex oldIndex = indexOf(oldItem);
    if (oldIndex.isValid())
      removeRows(0, rowCount(oldIndex), oldIndex);
    // regular changeItem
    SharedUiItemsTreeModel::changeItem(newItem, oldItem, qualifier);
    // insert host references rows
    QModelIndex newIndex = indexOf(newItem);
    if (newIndex.isValid()) {
      auto cluster = newItem.casted<const Cluster>();
      SharedUiItem nullItem;
      for (const Host &host: cluster.hosts()) {
        SharedUiItemsTreeModel::changeItem(
              HostReference(cluster.id(), host.id()), nullItem,
              "hostreference"_u8);
      }
    }
  } else {
    SharedUiItemsTreeModel::changeItem(newItem, oldItem, qualifier);
  }
}

void ClustersModel::determineItemPlaceInTree(
    const SharedUiItem &newItem, QModelIndex *parent, int *) {
  if (newItem.qualifier() == "hostreference"_u8) {
    auto hf = newItem.casted<const HostReference>();
    *parent = indexOf("cluster:"_u8+hf.cluster().toUtf8());
  }
}

bool ClustersModel::canDropMimeData(
    const QMimeData *data, Qt::DropAction action, int targetRow,
    int targetColumn, const QModelIndex &targetParent) const {
  Q_UNUSED(action)
  Q_UNUSED(targetRow)
  Q_UNUSED(targetColumn)
  if (!documentManager())
    return false; // cannot change data w/o dm
  if (!targetParent.isValid())
    return false; // cannot drop on root
  Utf8StringList ids = data->data(_suiQualifiedIdsListMimeType).split(' ');
  if (ids.isEmpty())
    return false; // nothing to drop
  for (auto qualified_id: ids) {
    auto qualifier = qualified_id.left(qualified_id.indexOf(':'));
    if (qualifier != "host"_u8 && qualifier != "hostreference"_u8)
      return false; // can only drop hosts
  }
  return true;
}

// Dropping on ClustersModel provide mean to reorder hosts of a given cluster
// and/or to add hosts to it.
// Therefore we support dropping a mix of hosts and hostreferences and make no
// assumption of whether they already belong to targeted cluster or not.
bool ClustersModel::dropMimeData(
    const QMimeData *data, Qt::DropAction action, int targetRow,
    int targetColumn, const QModelIndex &targetParent) {
  Q_UNUSED(action)
  Q_UNUSED(targetColumn)
  //qDebug() << "ClustersModel::dropMimeData";
  // find cluster to modify
  QModelIndex clusterIndex;
  if (targetParent.parent().isValid()) {
    clusterIndex = targetParent.parent();
    targetRow = targetParent.row();
  } else {
    clusterIndex = targetParent;
    targetRow = 0;
  }
  SharedUiItem clusterSui = itemAt(clusterIndex);
  Cluster &oldCluster = static_cast<Cluster&>(clusterSui);
  //qDebug() << "  dropping on:" << oldCluster.id() << targetRow;
  // build new hosts id list
  Utf8StringList ids = data->data(_suiQualifiedIdsListMimeType).split(' ');
  Utf8StringList oldHostsIds, droppedHostsIds, newHostsIds;
  for (const Host &host: oldCluster.hosts())
    oldHostsIds << host.id();
  for (auto qualified_id: ids) {
    Utf8String hostId;
    if (qualified_id.contains('~'))
      hostId = qualified_id.mid(qualified_id.indexOf('~')+1);
    else
      hostId = qualified_id.mid(qualified_id.indexOf(':')+1);
    droppedHostsIds += hostId;
  }
  int oldIndex = 0;
  for (; oldIndex < targetRow && oldIndex < oldHostsIds.size(); ++oldIndex) {
    const QString &id = oldHostsIds[oldIndex];
    if (!droppedHostsIds.contains(id))
      newHostsIds << id;
  }
  for (int droppedIndex = 0; droppedIndex < droppedHostsIds.size();
       ++droppedIndex)
    newHostsIds << droppedHostsIds[droppedIndex];
  for (; oldIndex < oldHostsIds.size(); ++oldIndex) {
    const QString &id = oldHostsIds[oldIndex];
    if (!droppedHostsIds.contains(id))
      newHostsIds << id;
  }
  //qDebug() << "  new hosts list:" << newHostsIds;
  //qDebug() << "  old one:" << oldHostsIds << "dropped one:" << droppedHostsIds;
  // update actual data item
  QList<Host> newHosts;
  for (auto id: newHostsIds) {
    SharedUiItem hostSui = documentManager()->itemById("host"_u8, id);
    Host &host = static_cast<Host&>(hostSui);
    newHosts << host;
  }
  Cluster newCluster = oldCluster;
  newCluster.setHosts(newHosts);
  documentManager()->changeItem(newCluster, oldCluster, "cluster"_u8);
  return true;
}
