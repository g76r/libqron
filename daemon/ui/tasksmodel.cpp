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
#include "tasksmodel.h"
#include <QDateTime>
#include "textviews.h"

#define COLUMNS 14

TasksModel::TasksModel(QObject *parent) : QAbstractListModel(parent) {
}

int TasksModel::rowCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return parent.isValid() ? 0 : _tasks.size();
}

int TasksModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return COLUMNS;
}

QVariant TasksModel::data(const QModelIndex &index, int role) const {
  if (index.isValid() && index.row() >= 0 && index.row() < _tasks.size()) {
    const Task t(_tasks.at(index.row()));
    switch(role) {
    case Qt::DisplayRole:
      switch(index.column()) {
      case 0:
        return t.id();
      case 1:
        return t.taskGroup().id();
      case 2:
        return t.label();
      case 3:
        return t.mean();
      case 4:
        return t.command();
      case 5:
        return t.target();
      case 6:
        return t.triggersAsString();
      case 7:
        return t.params().toString(false);
      case 8:
        return t.resourcesAsString();
      case 9:
        return t.lastExecution();
      case 10:
        return t.nextScheduledExecution();
      case 11:
        return t.fqtn();
      case 12:
        return t.maxInstances();
      case 13:
        return t.instancesCount();
      }
      break;
    case TextViews::HtmlPrefixRole:
      switch(index.column()) {
      case 0:
      case 11:
        return "<i class=\"icon-cog\"></i> ";
      case 1:
        return "<i class=\"icon-folder-open\"></i> ";
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

QVariant TasksModel::headerData(int section, Qt::Orientation orientation,
                                int role) const {
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
    switch(section) {
    case 0:
      return "Id";
    case 1:
      return "TaskGroup Id";
    case 2:
      return "Label";
    case 3:
      return "Mean";
    case 4:
      return "Command";
    case 5:
      return "Target";
    case 6:
      return "Triggers";
    case 7:
      return "Parameters";
    case 8:
      return "Resources";
    case 9:
      return "Last execution";
    case 10:
      return "Next execution";
    case 11:
      return "Fully qualified task name";
    case 12:
      return "Max instances";
    case 13:
      return "Instances count";
    }
  }
  return QVariant();
}

void TasksModel::setAllTasksAndGroups(QMap<QString, TaskGroup> groups,
                                      QMap<QString, Task> tasks) {
  Q_UNUSED(groups)
  beginResetModel();
  foreach (const Task task, tasks.values()) {
    int row;
    for (row = 0; row < _tasks.size(); ++row) {
      Task t2 = _tasks.at(row);
      // sort by taskgroupid then taskid
      if (task.taskGroup().id() < t2.taskGroup().id())
        break;
      if (task.taskGroup().id() == t2.taskGroup().id()
          && task.id() < t2.id())
        break;
    }
    _tasks.insert(row, task);
  }
  endResetModel();
}

void TasksModel::taskChanged(Task task) {
  for (int row = 0; row < _tasks.size(); ++row)
    if (_tasks.at(row).id() == task.id()) {
      QModelIndex index(createIndex(row, 0));
      emit dataChanged(index, index);
      return;
    }
}

void TasksModel::taskChanged(TaskRequest request) {
  taskChanged(request.task());
}