/* Copyright 2012-2023 Hallowyn and others.
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
#ifndef ALERT_H
#define ALERT_H

#include "libqron_global.h"
#include "modelview/shareduiitem.h"

class AlertData;
class AlertSubscription;

/** Class used to represent alert data during emission, in or between Alerter
 * and AlertChannel classes. */
class LIBQRONSHARED_EXPORT Alert : public SharedUiItem {
public:
  enum AlertStatus { Nonexistent, Rising, MayRise, Raised, Dropping,
                     Canceled };

  Alert();
  explicit Alert(const Utf8String &id,
                 const QDateTime &riseDate = QDateTime::currentDateTime());
  Alert(const Alert &other);
  Alert &operator=(const Alert &other) {
    SharedUiItem::operator=(other); return *this; }
  Alert::AlertStatus status() const;
  void setStatus(Alert::AlertStatus status);
  static Alert::AlertStatus statusFromString(const Utf8String &string);
  static Utf8String statusAsString(Alert::AlertStatus status);
  QString statusAsString() const { return statusAsString(status()); }
  /** Initial raise() call date, before rising period.
   * Won't change in the alert lifetime. */
  QDateTime riseDate() const;
  void setRiseDate(QDateTime riseDate);
  /** End of rise or cancellation delays.
   * Applicable to may_rise and dropping status. */
  QDateTime cancellationDate() const;
  void setCancellationDate(QDateTime cancellationDate);
  /** End of rising delay.
   * Applicable to rising and may_rise status. */
  QDateTime visibilityDate() const;
  void setVisibilityDate(QDateTime visibilityDate);
  /** Timestamp a raised alert was last reminded.
   * Only set by channels which handle reminders, not by Alerter. */
  QDateTime lastRemindedDate() const;
  void setLastRemindedDate(QDateTime lastRemindedDate);
  /** Subscription for which an Alert is notified to an AlertChannel.
   * Set by Alerter just before notifying an AlertChannel. */
  AlertSubscription subscription() const;
  void setSubscription(AlertSubscription subscription);
  /** Times the alert as to be notified. Should always be 1 except for emitted
   * alerts that have been aggregated. */
  int count() const;
  int incrementCount();
  void resetCount();
  /** Return a string of the form id()+" x "+count() if count() != 1, and only
   * id() otherwise. This is convenient to display aggregated alerts almost the
   * same way than other alerts in a human readable way. */
  Utf8String idWithCount() const;

private:
  AlertData *data();
  const AlertData *data() const {
    return reinterpret_cast<const AlertData*>(SharedUiItem::data()); }
};

Q_DECLARE_METATYPE(Alert)
Q_DECLARE_TYPEINFO(Alert, Q_MOVABLE_TYPE);

#endif // ALERT_H
