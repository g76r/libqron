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
#include "lastoccuredtexteventsmodel.h"
#include <QtDebug>

#define COLUMNS 2

LastOccuredTextEventsModel::LastOccuredTextEventsModel(QObject *parent, int maxsize)
  : QAbstractListModel(parent), _maxsize(maxsize) {
}

int LastOccuredTextEventsModel::rowCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return parent.isValid() ? 0 : _occuredEvents.size();
}

int LastOccuredTextEventsModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return COLUMNS;
}

QVariant LastOccuredTextEventsModel::data(const QModelIndex &index, int role) const {
  if (index.isValid() && index.row() >= 0
      && index.row() < _occuredEvents.size()) {
    if (role == Qt::DisplayRole) {
      const OccuredEvent ea(_occuredEvents.at(index.row()));
      switch(index.column()) {
      case 0:
        return ea._datetime;
      case 1:
        return ea._event;
      }
    } else if (role == _prefixRole && index.column() == 1)
      return _prefix;
  }
  return QVariant();
}

QVariant LastOccuredTextEventsModel::headerData(
    int section, Qt::Orientation orientation, int role) const {
  if (role == Qt::DisplayRole) {
    if (orientation == Qt::Horizontal) {
      switch(section) {
      case 0:
        return "Timestamp";
      case 1:
        return _eventName;
      }
    } else {
      return QString::number(section);
    }
  }
  // LATER htmlPrefix <i class="icon-bell"></i>
  return QVariant();
}

void LastOccuredTextEventsModel::eventOccured(QString event) {
  beginInsertRows(QModelIndex(), 0, 0);
  _occuredEvents.prepend(OccuredEvent(event));
  endInsertRows();
  if (_occuredEvents.size() > _maxsize) {
    beginRemoveRows(QModelIndex(), _maxsize, _maxsize);
    _occuredEvents.removeAt(_maxsize);
    endRemoveRows();
  }
}
