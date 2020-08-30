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
#include "parametrizednetworkrequest.h"
#include "log/log.h"
#include <QtDebug>

#define DEFAULT_REQUEST_CONTENT_TYPE "text/plain"

const QSet<QString> ParametrizedNetworkRequest::supportedParamNames {
  "method", "user", "password", "proto", "port", "payload", "content-type",
  "follow-redirect", "redirect-max" };

ParametrizedNetworkRequest::ParametrizedNetworkRequest(
    QString url, ParamSet params, ParamsProvider *paramsEvaluationContext,
    QString logTask, quint64 logExecId)
  : QNetworkRequest(), _logTask(logTask),
    _logExecId(logExecId ? QString::number(logExecId) : QString()),
    _params(params) {
  QUrl qurl(params.evaluate(url, paramsEvaluationContext));
  QString proto = params.value("proto", paramsEvaluationContext);
  if (!proto.isNull())
    qurl.setScheme(proto);
  QString user = params.value("user", qurl.userName(), paramsEvaluationContext);
  QString password = params.value("password", qurl.password(),
                                  paramsEvaluationContext);
  int port = params.valueAsInt("port", -1, paramsEvaluationContext);
  if (port > 0 && port < 65536)
    qurl.setPort(port);
  // must set basic auth through custom header since url user/password are
  // ignored by QNetworkAccessManager (at less for first request, before a 401)
  if (!user.isEmpty()) {
    QByteArray token = (user+":"+password).toUtf8();
    setRawHeader("Authorization", "Basic "+token.toBase64());
  }
  qurl.setUserName(QString());
  qurl.setPassword(QString());
  setUrl(qurl);
  _method = HttpRequest::methodFromText(
        params.value("method", "GET", paramsEvaluationContext).toUpper());
  QString contentType = params.value("content-type", paramsEvaluationContext);
  if (contentType.isNull()
      && (_method == HttpRequest::POST || _method == HttpRequest::PUT))
    contentType = DEFAULT_REQUEST_CONTENT_TYPE;
  if (!contentType.isNull())
    setHeader(QNetworkRequest::ContentTypeHeader, contentType);
  _rawPayloadFromParams = params.rawValue("payload");
#if QT_VERSION >= 0x050600
  bool followRedirect = params.valueAsBool("follow-redirect", false,
                                           paramsEvaluationContext);
  int redirectMax = params.valueAsInt("redirect-max", -1,
                                      paramsEvaluationContext);
  if (redirectMax > 0)
    followRedirect = true;
  if (followRedirect) {
    setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    if (redirectMax > 0)
      setMaximumRedirectsAllowed(redirectMax);
  }
#endif
}

QNetworkReply *ParametrizedNetworkRequest::performRequest(
    QNetworkAccessManager *nam, QString payload,
    ParamsProvider *payloadEvaluationContext) {
  QNetworkReply *reply = 0;
  QUrl url = this->url();
  if (payload.isNull())
    payload = _rawPayloadFromParams;
  payload = _params.evaluate(payload, payloadEvaluationContext);
  // LATER support proxy
  // LATER support ssl (https)
  if (url.isValid()) {
    if (_method != HttpRequest::NONE) {
      Log::debug(_logTask, _logExecId)
          << "exact "+HttpRequest::methodName(_method)+" URL to be called: "
          << url.toString(QUrl::RemovePassword);
      switch(_method) {
      case HttpRequest::GET:
        reply = nam->get(*this);
        break;
      case HttpRequest::POST:
        reply = nam->post(*this, payload.toUtf8());
        break;
      case HttpRequest::HEAD:
        reply = nam->head(*this);
        break;
      case HttpRequest::PUT:
        reply = nam->put(*this, payload.toUtf8());
        break;
      case HttpRequest::DELETE:
        reply = nam->deleteResource(*this);
        break;
      case HttpRequest::OPTIONS:
        reply = nam->sendCustomRequest(*this, "OPTIONS", payload.toUtf8());
        break;
      case HttpRequest::NONE:
      case HttpRequest::ANY:
        ; // cannot happen
      }
    } else {
      Log::error(_logTask, _logExecId)
          << "unsupported HTTP method: " << _method;
    }
  } else {
    Log::error(_logTask, _logExecId)
        << "unsupported HTTP URL: " << url.toString(QUrl::RemovePassword);
  }
  /*if (reply) {
    QObject::connect(reply, (void (QNetworkReply::*)(QNetworkReply::NetworkError))&QNetworkReply::error,
                     [=](QNetworkReply::NetworkError error){
      qDebug() << "  network reply error for parametrizedhttprequest:" << (long)reply << url << error;
    });
    QObject::connect(reply, &QNetworkReply::finished, [=](){
      qDebug() << "  network reply finished for parametrizedhttprequest:" << (long)reply << url;
    });
  }*/
  return reply;
}
