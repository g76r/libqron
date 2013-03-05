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
#include "event/event.h"
#include <QUrl>
#include <QTimer>

#define COLUMNS 20
#define SOON_EXECUTION_MILLIS 300000
// 300,000 ms = 5'
#define FULL_REFRESH_INTERVAL (SOON_EXECUTION_MILLIS/5)

TasksModel::TasksModel(QObject *parent) : QAbstractListModel(parent) {
  QTimer *timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(forceTimeRelatedDataRefresh()));
  timer->start(FULL_REFRESH_INTERVAL);
}

int TasksModel::rowCount(const QModelIndex &parent) const {
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
        return t.lastExecution().toString("yyyy-MM-dd hh:mm:ss,zzz");
      case 10:
        return t.nextScheduledExecution().toString("yyyy-MM-dd hh:mm:ss,zzz");
      case 11:
        return t.fqtn();
      case 12:
        return t.maxInstances();
      case 13:
        return t.instancesCount();
      case 14:
        return Event::toStringList(t.onstartEvents()).join(" ");
      case 15:
        return Event::toStringList(t.onsuccessEvents()).join(" ");
      case 16:
        return Event::toStringList(t.onfailureEvents()).join(" ");
      case 17:
        return QString::number(t.instancesCount())+" / "
            +QString::number(t.maxInstances());
      case 19: {
        QDateTime dt = t.lastExecution();
        if (!dt.isNull())
          return dt.toString("yyyy-MM-dd hh:mm:ss,zzz")
              .append(t.lastSuccessful() ? " success" : " failure");
      }
      }
      break;
    case TextViews::HtmlPrefixRole:
      switch(index.column()) {
      case 0:
      case 11:
        return "<i class=\"glyphicon-cogwheel\"></i> ";
      case 1:
        return "<i class=\"glyphicon-cogwheels\"></i> ";
      case 6: {
        QString prefix;
        if (!t.enabled())
          prefix = "<i class=\"icon-ban-circle\"></i> disabled ";
        if (t.triggersAsString().isEmpty())
          prefix += "<i class=\"icon-remove\"></i> no trigger ";
        return prefix;
      }
      case 10: {
        QDateTime dt = t.nextScheduledExecution();
        if (!dt.isNull()
            && dt.toMSecsSinceEpoch()-QDateTime::currentMSecsSinceEpoch()
            < SOON_EXECUTION_MILLIS)
          return "<i class=\"glyphicon-alarm\"></i> ";
        break;
      }
      case 18: {
        QString fqtn = t.fqtn();
        bool enabled = t.enabled();
        return
            /* requestTask */
            " <span class=\"label label-important\" "
            "title=\"Request execution\">"
            "<a href=\"do?event=requestTask&fqtn="
            +fqtn+"\"><i class=\"icon-play icon-white\"></i></a></span>"
            /* {enable,disable}Task */
            " <span class=\"label label-"+(enabled?"important":"warning")
            +"\" title=\""+(enabled?"Disable":"Enable")+"\">"
            "<a href=\"do?event=enableTask&fqtn="+fqtn+"&enable="
            +(enabled?"false":"true")+"\"><i class=\"icon-ban-circle"
            //+(enabled?"ban-circle":"ok-circle")
            +" icon-white\"></i></a></span>"
            /* log */
            " <span class=\"label label-info\" title=\"Log\">"
            "<a target=\"_blank\" href=\"../rest/txt/log/all/v1?filter=%20"
            +fqtn+"/\"><i class=\"icon-th-list icon-white\"></i></a></span>";
      }
      case 19: {
        QDateTime dt = t.lastExecution();
        if (!dt.isNull() && !t.lastSuccessful())
          return "<i class=\"icon-minus-sign\"></i> ";
      }
      default:
        ;
      }
      break;
    case TextViews::HtmlSuffixRole:
      switch(index.column()) {
      case 18: {
        QString infourl = t.infourl();
        if (!infourl.isEmpty())
          return " <span class=\"label label-info\"><a target=\"_blank\" "
              "title=\"Information / Documentation\""
              "href=\""+infourl+"\"><i class=\"icon-info-sign icon-white\">"
              "</i></a></span>";
      }
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
    case 14:
      return "On start";
    case 15:
      return "On success";
    case 16:
      return "On failure";
    case 17:
      return "Instances / max";
    case 18:
      return "Actions";
    case 19:
      return "Last execution status";
    }
  }
  return QVariant();
}

void TasksModel::setAllTasksAndGroups(QMap<QString, TaskGroup> groups,
                                      QMap<QString, Task> tasks) {
  Q_UNUSED(groups)
  beginResetModel();
  _tasks.clear();
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
      emit dataChanged(createIndex(row, 0), createIndex(row, COLUMNS-1));
      return;
    }
}

void TasksModel::forceTimeRelatedDataRefresh() {
  int size = _tasks.size();
  if (size)
    emit dataChanged(createIndex(0, 10), createIndex(size-1, 10));
}
