/* Copyright 2014-2023 Hallowyn and others.
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
#include "confighistorymodel.h"

ConfigHistoryModel::ConfigHistoryModel(QObject *parent, int maxrows)
  : SharedUiItemsTableModel(parent) {
  setHeaderDataFromTemplate(ConfigHistoryEntry({}, {}, {}, {}));
  setMaxrows(maxrows);
}

void ConfigHistoryModel::historyReset(QList<ConfigHistoryEntry> history) {
  std::reverse(history.begin(), history.end());
  setItems(history);
}

void ConfigHistoryModel::historyEntryAppended(ConfigHistoryEntry historyEntry) {
  insertItemAt(historyEntry, 0);
}
