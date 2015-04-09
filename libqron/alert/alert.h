/* Copyright 2012-2014 Hallowyn and others.
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
#ifndef ALERT_H
#define ALERT_H

#include "libqron_global.h"
#include <QSharedDataPointer>
#include "config/alertrule.h"
#include <QDateTime>
#include "util/paramsprovider.h"

class AlertData;

/** Class used to represent alert data during emission, in or between Alerter
 * and AlertChannel classes. */
class LIBQRONSHARED_EXPORT Alert : public ParamsProvider {
  QSharedDataPointer<AlertData> d;

public:
  Alert();
  Alert(QString id, AlertRule rule,
        QDateTime datetime = QDateTime::currentDateTime());
  Alert(const Alert&);
  Alert &operator=(const Alert &other);
  /** Compare id()'s */
  bool operator<(const Alert &other) const;
  ~Alert();
  QString id() const;
  AlertRule rule() const;
  QDateTime datetime() const;
  QVariant paramValue(QString key, QVariant defaultValue = QVariant(),
                      QSet<QString> alreadyEvaluated = QSet<QString>()) const;
};

#endif // ALERT_H
