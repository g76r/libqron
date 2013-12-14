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
#include "alert.h"
#include <QSharedData>

class AlertData : public QSharedData {
public:
  QString _id;
  AlertRule _rule;
  QDateTime _datetime;
  AlertData(const QString id = QString(), AlertRule rule = AlertRule(),
            QDateTime datetime = QDateTime::currentDateTime())
    : _id(id), _rule(rule), _datetime(datetime) { }
};

Alert::Alert() : d(new AlertData) {
}

Alert::Alert(QString id, AlertRule rule, QDateTime datetime)
  : d(new AlertData(id, rule, datetime)) {
}

Alert::Alert(const Alert &rhs) : ParamsProvider(), d(rhs.d) {
}

Alert &Alert::operator=(const Alert &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

bool Alert::operator<(const Alert &other) const {
  return this != &other && d->_id < other.d->_id;
}

Alert::~Alert() {
}

QString Alert::id() const {
  return d ? d->_id : QString();
}

AlertRule Alert::rule() const {
  return d ? d->_rule : AlertRule();
}

QDateTime Alert::datetime() const {
  return d ? d->_datetime : QDateTime();
}

QVariant Alert::paramValue(QString key, QVariant defaultValue) const {
  if (key == "!alertid") {
    return id();
  } else if (key == "!alertdate") {
    return datetime().toString("yyyy-MM-dd hh:mm:ss,zzz");
  }
  return defaultValue;
}
