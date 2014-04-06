/* Copyright 2013-2014 Hallowyn and others.
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
#include "config/eventsubscription.h"

TaskGroupsModel::TaskGroupsModel(QObject *parent)
  : SharedUiItemsTableModel(parent) {
}

void TaskGroupsModel::configReset(SchedulerConfig config) {
  QList<SharedUiItem> items;
  foreach (SharedUiItem item, config.tasksGroups().values())
    items.append(item);
  qSort(items);
  resetItems(items);
}
