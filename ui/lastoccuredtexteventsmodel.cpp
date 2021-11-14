/* Copyright 2013-2021 Hallowyn and others.
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
#include "lastoccuredtexteventsmodel.h"
#include <QtDebug>

#define COLUMNS 3

LastOccuredTextEventsModel::LastOccuredTextEventsModel(QObject *parent,
                                                       int maxrows)
  : QAbstractTableModel(parent), _maxrows(maxrows) {
}

int LastOccuredTextEventsModel::rowCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return parent.isValid() ? 0 : _occuredEvents.size();
}

int LastOccuredTextEventsModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent)
  return COLUMNS+_additionnalColumnsHeaders.size();
}

QVariant LastOccuredTextEventsModel::data(const QModelIndex &index, int role) const {
  if (index.isValid() && index.row() >= 0
      && index.row() < _occuredEvents.size()) {
    const OccuredEvent &oe(_occuredEvents.at(index.row()));
    if (role == Qt::DisplayRole) {
      switch(index.column()) {
      case 0:
        return oe._datetime.toString("yyyy-MM-dd hh:mm:ss,zzz");
      case 1:
        return oe._event;
      case 2:
        return oe._type;
      }
    }
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
      case 2:
        return "Type";
      default:
        return _additionnalColumnsHeaders.value(section-COLUMNS);
      }
    } else {
      return QString::number(section);
    }
  }
  return QVariant();
}

void LastOccuredTextEventsModel::eventOccured(QString event) {
  typedEventOccured(event, 0);
}

void LastOccuredTextEventsModel::typedEventOccured(QString event, int type) {
  beginInsertRows(QModelIndex(), 0, 0);
  _occuredEvents.prepend(OccuredEvent(event, type));
  endInsertRows();
  if (_occuredEvents.size() > _maxrows) {
    beginRemoveRows(QModelIndex(), _maxrows, _maxrows);
    _occuredEvents.removeAt(_maxrows);
    endRemoveRows();
  }
}
