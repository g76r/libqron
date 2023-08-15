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
#include "urlalertchannel.h"
#include "sysutil/parametrizednetworkrequest.h"
#include "sysutil/parametrizedudpsender.h"
#include "alerter.h"
#include "config/alerterconfig.h"

UrlAlertChannel::UrlAlertChannel(Alerter *alerter)
  : AlertChannel(alerter), _nam(0) {
  _thread->setObjectName("UrlAlertChannelThread");
}

void UrlAlertChannel::doNotifyAlert(Alert alert) {
  if (!_nam) {
    // must be created here since the constructor is called by another thread
    // and one cannot create an QObject which parent lies in another thread
    _nam = new QNetworkAccessManager(this);
    connect(_nam, &QNetworkAccessManager::finished,
            this, &UrlAlertChannel::replyFinished);
  }
  QString address = alert.subscription().address(alert), message;
  ParamSet params = alert.subscription().params();
  switch(alert.status()) {
  case Alert::Nonexistent:
  case Alert::Raised:
    if (!alert.subscription().notifyEmit())
      return;
    if (params.contains(QStringLiteral("emitaddress")))
      address = params.rawValue(QStringLiteral("emitaddress"));
    if (params.contains(QStringLiteral("emitmethod")))
      params.setValue(QStringLiteral("method"),
                      params.rawValue(QStringLiteral("emitmethod")));
    message = alert.subscription().emitMessage(alert);

    break;
  case Alert::Canceled:
    if (!alert.subscription().notifyCancel())
      return;
    if (params.contains(QStringLiteral("canceladdress")))
      address = params.rawValue(QStringLiteral("canceladdress"));
    if (params.contains(QStringLiteral("cancelmethod")))
      params.setValue(QStringLiteral("method"),
                      params.rawValue(QStringLiteral("cancelmethod")));
    message = alert.subscription().cancelMessage(alert);
    break;
  case Alert::Rising:
  case Alert::MayRise:
  case Alert::Dropping:
    ; // should never happen
  }
  // LATER support for binary messages
  AlertPseudoParamsProvider ppp = alert.pseudoParams();
  if (address.startsWith("udp:", Qt::CaseInsensitive)) {
    ParametrizedUdpSender sender(address, params, &ppp);
    sender.performRequest(message, &ppp);
  } else {
    ParametrizedNetworkRequest request(address, params, &ppp);
    request.performRequest(_nam, message, &ppp);
    /*if (reply) {
    QObject::connect(reply, (void (QNetworkReply::*)(QNetworkReply::NetworkError))&QNetworkReply::error,
                     [=](QNetworkReply::NetworkError error){
      qDebug() << "network reply error for alert:" << (long)reply << address << error;
    });
    QObject::connect(reply, &QNetworkReply::finished, [=](){
      qDebug() << "network reply finished for alert:" << (long)reply << address;
    });
  }*/
  }
}

void UrlAlertChannel::replyFinished(QNetworkReply *reply) {
  if (reply)
    reply->deleteLater();
}
