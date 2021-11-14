/* Copyright 2012-2021 Hallowyn and others.
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
#include "alertchannel.h"
#include <QThread>
#include <QMetaObject>
#include <QMetaType>
#include "alerter.h"

AlertChannel::AlertChannel(Alerter *alerter)
  : QObject(0), _thread(new QThread), _alerter(alerter) {
  // can't have a parent because it lives it its own thread
  connect(this, &QObject::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  _thread->start();
  moveToThread(_thread);
  //qDebug() << "AlertChannel" << this;
}

void AlertChannel::notifyAlert(Alert alert) {
  QMetaObject::invokeMethod(this, [this,alert]() {
    doNotifyAlert(alert);
  });
}

void AlertChannel::setConfig(AlerterConfig config) {
  Q_UNUSED(config)
}

AlertChannel::~AlertChannel() {
  //qDebug() << "~AlertChannel" << this;
}
