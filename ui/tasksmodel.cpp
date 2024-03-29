/* Copyright 2013-2023 Hallowyn and others.
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
#include "tasksmodel.h"
#include "config/eventsubscription.h"
#include <QTimer>

// 60,000 ms = 1'
// should stay below HtmlTaskItemDelegate's SOON_EXECUTION_MILLIS
#define PERIODIC_REFRESH_INTERVAL 60'000

TasksModel::TasksModel(QObject *parent)
  : SharedUiItemsTableModel(parent) {
  setHeaderDataFromTemplate(Task::dummyTask());
  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &TasksModel::periodicDataRefresh);
  timer->start(PERIODIC_REFRESH_INTERVAL);
}

QVariant TasksModel::data(const QModelIndex &index, int role) const {
  switch(role) {
  case Qt::DisplayRole:
    switch(index.column()) {
    case 18:
      // MAYDO move that to html delegate (which needs having access to the Task)
      // TODO support for action columns larger than HtmlItemDelegate::maxCellContentLength
      if (!_customActions.isEmpty()) {
        Task t = _tasks.value(index.row());
        return PercentEvaluator::eval_utf16(_customActions, &t);
      }
      break;
    }
  }
  return SharedUiItemsModel::data(index, role);
}

void TasksModel::periodicDataRefresh() {
  int size = _tasks.size();
  if (size) {
    // Simulate "Next Execution" change to make views recompute their "soon to
    // be executed" indicator, which can change status even when the next
    // execution timestamp does not change.
    emit dataChanged(index(0, 10), index(size-1, 10));
  }
}
