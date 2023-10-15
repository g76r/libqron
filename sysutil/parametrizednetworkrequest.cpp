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
#include "parametrizednetworkrequest.h"
#include "util/paramsprovidermerger.h"

#define DEFAULT_REQUEST_CONTENT_TYPE "text/plain"_u8

const Utf8StringSet ParametrizedNetworkRequest::supportedParamNames {
  "method", "user", "password", "proto", "port", "payload", "content-type",
  "follow-redirect", "redirect-max" };

ParametrizedNetworkRequest::ParametrizedNetworkRequest(
    const Utf8String &url, const ParamSet &params,
    const ParamsProvider *paramsEvaluationContext,
    const Utf8String &logTask, quint64 logExecId)
  : QNetworkRequest(), _logTask(logTask),
    _logExecId(logExecId ? QString::number(logExecId) : QString()),
    _params(params) {
  auto ppm = ParamsProviderMerger(params)(paramsEvaluationContext);
  QUrl qurl(PercentEvaluator::eval_utf16(url, &ppm));
  auto proto = params.paramUtf8("proto"_u8, paramsEvaluationContext);
  if (!proto.isNull())
    qurl.setScheme(proto);
  auto user = params.paramUtf8("user"_u8, qurl.userName(),
                               paramsEvaluationContext);
  auto password = params.paramUtf8("password"_u8, qurl.password(),
                                   paramsEvaluationContext);
  int port = params.paramNumber<int>("port"_u8, -1, paramsEvaluationContext);
  if (port > 0 && port < 65536)
    qurl.setPort(port);
  // must set basic auth through custom header since url user/password are
  // ignored by QNetworkAccessManager (at less for first request, before a 401)
  if (!user.isEmpty())
    setRawHeader("Authorization", ("Basic "+user+":"+password).toBase64());
  qurl.setUserName({});
  qurl.setPassword({});
  setUrl(qurl);
  _method = HttpRequest::methodFromText(
        params.paramUtf8("method"_u8, "GET"_u8,
                         paramsEvaluationContext).toUpper());
  auto contentType = params.paramUtf8(
        "content-type"_u8, paramsEvaluationContext);
  if (contentType.isNull()
      && (_method == HttpRequest::POST || _method == HttpRequest::PUT))
    contentType = DEFAULT_REQUEST_CONTENT_TYPE;
  if (!contentType.isNull())
    setHeader(QNetworkRequest::ContentTypeHeader, contentType);
  _rawPayloadFromParams = params.paramRawUtf8("payload"_u8);
  bool followRedirect = params.paramNumber<bool>(
                          "follow-redirect"_u8, false, paramsEvaluationContext);
  int redirectMax = params.paramNumber<int>(
                      "redirect-max"_u8, -1, paramsEvaluationContext);
  if (redirectMax > 0)
    followRedirect = true;
  setAttribute(QNetworkRequest::RedirectPolicyAttribute,
               followRedirect ? QNetworkRequest::NoLessSafeRedirectPolicy
                              : QNetworkRequest::ManualRedirectPolicy);
  if (redirectMax > 0)
    setMaximumRedirectsAllowed(redirectMax);
}

QNetworkReply *ParametrizedNetworkRequest::performRequest(
    QNetworkAccessManager *nam, const Utf8String &payload,
    const ParamsProvider *payloadEvaluationContext) {
  QNetworkReply *reply = 0;
  QUrl url = this->url();
  auto effective_payload = payload.isNull() ? _rawPayloadFromParams : payload;
  auto ppm = ParamsProviderMerger(payloadEvaluationContext)(_params);
  effective_payload = PercentEvaluator::eval_utf8(effective_payload, &ppm);
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
        reply = nam->post(*this, effective_payload);
        break;
      case HttpRequest::HEAD:
        reply = nam->head(*this);
        break;
      case HttpRequest::PUT:
        reply = nam->put(*this, effective_payload);
        break;
      case HttpRequest::DELETE:
        reply = nam->deleteResource(*this);
        break;
      case HttpRequest::OPTIONS:
        reply = nam->sendCustomRequest(*this, "OPTIONS", effective_payload);
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
