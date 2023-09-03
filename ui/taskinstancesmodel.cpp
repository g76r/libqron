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
#include "taskinstancesmodel.h"

TaskInstancesModel::TaskInstancesModel(QObject *parent, int maxrows,
                                     bool keepFinished)
  : SharedUiItemsTableModel(parent), _keepFinished(keepFinished) {
  setHeaderDataFromTemplate(TaskInstance(Task(), false, ParamSet(), 0));
  setDefaultInsertionPoint(SharedUiItemsTableModel::FirstItem);
  setMaxrows(maxrows);
}

QVariant TaskInstancesModel::data(const QModelIndex &index, int role) const {
  switch(role) {
  case Qt::DisplayRole:
    switch(index.column()) {
    case 8:
      // MAYDO move that to html delegate (which needs having access to the TaskInstance)
      if (!_customActions.isEmpty()) {
        SharedUiItem sui = itemAt(index.row());
        //qDebug() << "TaskInstancesModel::data(8, DisplayRole)" << index.row() << sui.qualifiedId();
        if (sui.idQualifier() == "taskinstance"_u8) {
          TaskInstance &ti = reinterpret_cast<TaskInstance&>(sui);
          return PercentEvaluator::eval_utf16(_customActions, &ti);
        }
        break;
      }
      break;
    }
  }
  return SharedUiItemsModel::data(index, role);
}

void TaskInstancesModel::changeItem(
    SharedUiItem newItem, SharedUiItem oldItem, QByteArray idQualifier) {
  //qDebug() << "TaskInstancesModel::changeItem" << newItem.qualifiedId() << oldItem.qualifiedId();
  if (! _keepFinished && idQualifier == "taskinstance"_u8) {
    TaskInstance &newTaskInstance = reinterpret_cast<TaskInstance&>(newItem);
    if (newTaskInstance.isFinished())
      newItem = SharedUiItem();
  }
  SharedUiItemsTableModel::changeItem(newItem, oldItem, idQualifier);
}
