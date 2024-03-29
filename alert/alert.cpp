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
#include "alert.h"
#include "config/alertsubscription.h"
#include "modelview/templatedshareduiitemdata.h"

class AlertData : public SharedUiItemDataBase<AlertData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  Utf8String _id;
  Alert::AlertStatus _status;
  QDateTime _riseDate, _visibilityDate, _cancellationDate, _lastReminderDate;
  AlertSubscription _subscription;
  int _count;
  AlertData(const Utf8String &id = {},
            const QDateTime &riseDate = QDateTime::currentDateTime())
    : _id(id), _status(Alert::Nonexistent), _riseDate(riseDate), _count(1) { }
  Utf8String id() const override { return _id; }
  QVariant uiData(int section, int role) const override;
  Utf8String idWithCount() const {
    return _count > 1 ? _id+" x "+Utf8String::number(_count) : _id; }
};

QVariant AlertData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return Alert::statusAsString(_status);
    case 2:
      return _riseDate.toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s);
    case 3:
      return _status == Alert::Rising || _status == Alert::MayRise
          ? _visibilityDate.toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s)
          : QVariant();
    case 4:
      return _status == Alert::MayRise || _status == Alert::Dropping
          ? _cancellationDate.toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s)
          : QVariant();
    case 5:
      break; // actions
    case 6:
      return _count;
    case 7:
      return idWithCount();
    case 8:
      return Alert::statusAsString(_status);
    }
    break;
  default:
    ;
  }
  return {};
}

Alert::Alert() : SharedUiItem() {
}

Alert::Alert(const Utf8String &id, const QDateTime &datetime)
  : SharedUiItem(new AlertData(id, datetime)) {
}

Alert::Alert(const Alert &other) : SharedUiItem(other) {
}

QDateTime Alert::riseDate() const {
  const AlertData *d = data();
  return d ? d->_riseDate : QDateTime();
}

void Alert::setRiseDate(QDateTime riseDate) {
  AlertData *d = data();
  if (d)
    d->_riseDate = riseDate;
}

QDateTime Alert::cancellationDate() const {
  const AlertData *d = data();
  return d ? d->_cancellationDate : QDateTime();
}

void Alert::setCancellationDate(QDateTime cancellationDate) {
  AlertData *d = data();
  if (d)
    d->_cancellationDate = cancellationDate;
}

QDateTime Alert::visibilityDate() const {
  const AlertData *d = data();
  return d ? d->_visibilityDate : QDateTime();
}

void Alert::setVisibilityDate(QDateTime visibilityDate) {
  AlertData *d = data();
  if (d)
    d->_visibilityDate = visibilityDate;
}

Alert::AlertStatus Alert::status() const {
  const AlertData *d = data();
  return d ? d->_status : Alert::Nonexistent;
}

void Alert::setStatus(Alert::AlertStatus status) {
  AlertData *d = data();
  if (d)
    d->_status = status;
}

QDateTime Alert::lastRemindedDate() const {
  const AlertData *d = data();
  return d ? d->_lastReminderDate : QDateTime();
}

void Alert::setLastRemindedDate(QDateTime lastRemindedDate) {
  AlertData *d = data();
  if (d)
    d->_lastReminderDate = lastRemindedDate;
}

AlertSubscription Alert::subscription() const {
  const AlertData *d = data();
  return d ? d->_subscription : AlertSubscription();
}

void Alert::setSubscription(AlertSubscription subscription) {
  AlertData *d = data();
  if (d)
    d->_subscription = subscription;
}

AlertData *Alert::data() {
  return detachedData<AlertData>();
}

static QMap<Alert::AlertStatus,Utf8String> _statusToText {
  { Alert::Nonexistent, "nonexistent" },
  { Alert::Rising, "rising" },
  { Alert::MayRise, "may_rise" },
  { Alert::Raised, "raised" },
  { Alert::Dropping, "dropping" },
  { Alert::Canceled, "canceled" },
};

static RadixTree<Alert::AlertStatus> _statusFromText =
    RadixTree<Alert::AlertStatus>::reversed(_statusToText);

Alert::AlertStatus Alert::statusFromString(const Utf8String &string) {
  return _statusFromText.value(string, Alert::Nonexistent);
}

Utf8String Alert::statusAsString(Alert::AlertStatus status) {
  return _statusToText.value(status, "nonexistent"_u8);
}

int Alert::count() const {
  const AlertData *d = data();
  return d ? d->_count : 0;
}

int Alert::incrementCount() {
  AlertData *d = data();
  return d ? ++(d->_count) : 0;
}

void Alert::resetCount() {
  AlertData *d = data();
  if (d)
    d->_count = 0;
}

Utf8String Alert::idWithCount() const {
  const AlertData *d = data();
  return d ? d->idWithCount() : Utf8String();
}

const Utf8String AlertData::_qualifier = "alert";

const Utf8StringIndexedConstList AlertData::_sectionNames {
  "alertid", // 0
  "alertstatus",
  "risedate",
  "visibilitydate",
  "cancellationdate",
  "actions", // 5
  "count",
  "idwithcount",
  "alertstatus"
};

const Utf8StringIndexedConstList AlertData::_headerNames {
  "Id", // 0
  "Status",
  "Rise Date",
  "Visibility Date",
  "Cancellation Date",
  "Actions", // 5
  "Count",
  "Id With Count",
  "Status"
};
