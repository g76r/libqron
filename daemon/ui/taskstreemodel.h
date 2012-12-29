/* Copyright 2012 Hallowyn and others.
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
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TASKSTREEMODEL_H
#define TASKSTREEMODEL_H

#include "treemodelwithstructure.h"
#include "data/taskgroup.h"
#include "data/task.h"

class TasksTreeModel : public TreeModelWithStructure {
  Q_OBJECT
  QMap<QString,TaskGroup> _groups;
  QMap<QString,Task> _tasks;

public:
  explicit TasksTreeModel(QObject *parent = 0);
  int columnCount(const QModelIndex &parent) const;
  QVariant data(const QModelIndex &index, int role) const;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const;

public slots:
  void setAllTasksAndGroups(const QMap<QString,TaskGroup> groups,
                            const QMap<QString,Task> tasks);
};

#endif // TASKSTREEMODEL_H
