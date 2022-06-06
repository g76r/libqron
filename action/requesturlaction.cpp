/* Copyright 2014-2022 Hallowyn and others.
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
#include "requesturlaction.h"
#include "action_p.h"
#include "log/log.h"
#include "config/configutils.h"
#include "sysutil/parametrizednetworkrequest.h"
#include "sysutil/parametrizedudpsender.h"
#include "util/paramsprovidermerger.h"
#include <QNetworkReply>

class RequestUrlGlobalNetworkActionHub {
public:
  QNetworkAccessManager *_nam;
  RequestUrlGlobalNetworkActionHub() : _nam(new QNetworkAccessManager) { }
  RequestUrlGlobalNetworkActionHub(const RequestUrlGlobalNetworkActionHub&) = delete;
};

Q_GLOBAL_STATIC(RequestUrlGlobalNetworkActionHub, globalNetworkActionHub)

class LIBQRONSHARED_EXPORT RequestUrlActionData : public ActionData {
public:
  QString _address, _message;
  ParamSet _params;

  RequestUrlActionData(QString address = QString(), QString message = QString(),
                ParamSet params = ParamSet())
    : _address(address), _message(message), _params(params) {
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    ParamsProviderMergerRestorer ppmr(context);
    context->prepend(_params);
    // LATER support binary payloads
    if (_address.startsWith("udp:", Qt::CaseInsensitive)) {
      // LATER run UDP in a separate thread to avoid network/dns/etc. hangups
      ParametrizedUdpSender sender(
          _address, _params, context, instance.task().id(),
          instance.idAsLong());
      sender.performRequest(_message, context);
    } else {
      ParametrizedNetworkRequest request(
            _address, _params, context, instance.task().id(),
          instance.idAsLong());
      QNetworkReply *reply = request.performRequest(
            globalNetworkActionHub->_nam, _message, context);
      if (reply) {
        QObject::connect(reply, &QNetworkReply::errorOccurred,
                         reply, &QObject::deleteLater);
        QObject::connect(reply, &QNetworkReply::finished,
                         reply, &QNetworkReply::deleteLater);
      }
    }
  }
  QString toString() const override {
    return "requesturl{ "+_address+" }";
  }
  QString actionType() const override {
    return QStringLiteral("requesturl");
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _message);
    node.appendChild(PfNode(QStringLiteral("address"), _address));
    ConfigUtils::writeParamSet(&node, _params, QStringLiteral("param"));
    return node;
  }
};

RequestUrlAction::RequestUrlAction(Scheduler *scheduler, PfNode node)
  : Action(new RequestUrlActionData(
      node.attribute("address"), node.contentAsString(),
      ParamSet(node, ParametrizedNetworkRequest::supportedParamNames
                       +ParametrizedUdpSender::supportedParamNames))) {
  Q_UNUSED(scheduler)
}

RequestUrlAction::RequestUrlAction(const RequestUrlAction &rhs) : Action(rhs) {
}
