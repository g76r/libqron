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
#ifndef CALENDAR_H
#define CALENDAR_H

#include <QSharedDataPointer>
#include <QDate>
#include "pf/pfnode.h"

class CalendarData;

class Calendar {
  QSharedDataPointer<CalendarData> d;

public:
  Calendar();
  Calendar(const Calendar &);
  Calendar(PfNode node);
  Calendar &operator=(const Calendar &);
  ~Calendar();
  Calendar &append(QDate begin, QDate end, bool include);
  inline Calendar &append(QDate date, bool include) {
    return append(date, date, include); }
  inline Calendar &append(bool include) {
    return append(QDate(), QDate(), include); }
  bool isIncluded(QDate date) const;
  bool isNull() const;
  void clear();
  QString toPf(bool useNameIfAvaillable = false) const;
  /** Enumerate rules in a human readable fashion */
  QString rulesAsString() const;
  /** Can return QString("") if anonymous */
  QString name() const;
};

#endif // CALENDAR_H
