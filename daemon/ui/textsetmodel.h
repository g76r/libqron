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
 * along with qron. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TEXTSETMODEL_H
#define TEXTSETMODEL_H

#include <QAbstractListModel>
#include <QStringList>

class TextSetModel : public QAbstractListModel {
  Q_OBJECT
  QStringList _values;

public:
  explicit TextSetModel(QObject *parent = 0);
  int rowCount(const QModelIndex &parent) const;
  QVariant data(const QModelIndex &index, int role) const;

public slots:
  void insertValue(const QString value);
  void removeValue(const QString value);
};

#endif // TEXTSETMODEL_H
