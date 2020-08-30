/* Copyright 2014-2017 Hallowyn and others.
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
  void trigger(EventSubscription subscription, ParamSet eventContext,
               TaskInstance taskContext) const {
    Q_UNUSED(subscription)
    // LATER support binary payloads
    TaskInstancePseudoParamsProvider ppp = taskContext.pseudoParams();
    ParamsProviderMerger evaluationContext =
        ParamsProviderMerger(_params)(eventContext)(&ppp);
    if (_address.startsWith("udp:", Qt::CaseInsensitive)) {
      // LATER run UDP in a separate thread to avoid network/dns/etc. hangups
      ParametrizedUdpSender sender(_address, _params, &evaluationContext,
                                   taskContext.task().id(),
                                   taskContext.idAsLong());
      sender.performRequest(_message, &evaluationContext);
    } else {
      ParametrizedNetworkRequest request(
            _address, _params, &evaluationContext, taskContext.task().id(),
            taskContext.idAsLong());
      QNetworkReply *reply = request.performRequest(
            globalNetworkActionHub->_nam, _message, &evaluationContext);
      if (reply) {
        // FIXME use errorOccurred() instead of (non signal!) error()
        QObject::connect(reply, static_cast<void(QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
                         reply, &QNetworkReply::deleteLater);
        QObject::connect(reply, &QNetworkReply::finished,
                         reply, &QNetworkReply::deleteLater);
      }
    }
  }
  QString toString() const {
    return "requesturl{ "+_address+" }";
  }
  QString actionType() const {
    return QStringLiteral("requesturl");
  }
  PfNode toPfNode() const{
    PfNode node(actionType(), _message);
    node.appendChild(PfNode(QStringLiteral("address"), _address));
    ConfigUtils::writeParamSet(&node, _params, QStringLiteral("param"));
    return node;
  }
};

RequestUrlAction::RequestUrlAction(Scheduler *scheduler, PfNode node)
  : Action(new RequestUrlActionData(
             node.attribute("address"), node.contentAsString(),
             ConfigUtils::loadParamSet(
               node, ParametrizedNetworkRequest::supportedParamNames
               +ParametrizedUdpSender::supportedParamNames))) {
  Q_UNUSED(scheduler)
}

RequestUrlAction::RequestUrlAction(const RequestUrlAction &rhs) : Action(rhs) {
}
