/* Copyright 2013 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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
#include "taskgroupsmodel.h"
#include <QDateTime>
#include "textviews.h"
#include "event/event.h"

#define COLUMNS 6

TaskGroupsModel::TaskGroupsModel(QObject *parent) : QAbstractListModel(parent) {
}

int TaskGroupsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : _groups.size();
}

int TaskGroupsModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return COLUMNS;
}

QVariant TaskGroupsModel::data(const QModelIndex &index, int role) const {
  if (index.isValid() && index.row() >= 0 && index.row() < _groups.size()) {
    const TaskGroup tg(_groups.at(index.row()));
    switch(role) {
    case Qt::DisplayRole:
      switch(index.column()) {
      case 0:
        return tg.id();
      case 1:
        return tg.label();
      case 2:
        return tg.params().toString(false);
      case 3:
        return Event::toStringList(tg.onstartEvents()).join(" ");
      case 4:
        return Event::toStringList(tg.onsuccessEvents()).join(" ");
      case 5:
        return Event::toStringList(tg.onfailureEvents()).join(" ");
      }
      break;
    case TextViews::HtmlPrefixRole:
      switch(index.column()) {
      case 0:
        return "<i class=\"glyphicon-cogwheels\"></i> ";
      default:
        ;
      }
      break;
    default:
      ;
    }
  }
  return QVariant();
}

QVariant TaskGroupsModel::headerData(int section, Qt::Orientation orientation,
                                int role) const {
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
    switch(section) {
    case 0:
      return "Id";
    case 1:
      return "Label";
    case 2:
      return "Parameters";
    case 3:
      return "On start";
    case 4:
      return "On success";
    case 5:
      return "On failure";
    }
  }
  return QVariant();
}

void TaskGroupsModel::setAllTasksAndGroups(QMap<QString, TaskGroup> groups,
                                      QMap<QString, Task> tasks) {
  Q_UNUSED(tasks)
  beginResetModel();
  foreach (const TaskGroup group, groups.values()) {
    int row;
    for (row = 0; row < _groups.size(); ++row) {
      TaskGroup g2 = _groups.at(row);
      // sort by taskgroupid
      if (group.id() < g2.id())
        break;
    }
    _groups.insert(row, group);
  }
  endResetModel();
}
