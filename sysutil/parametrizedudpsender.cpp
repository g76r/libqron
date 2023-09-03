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
#include "parametrizedudpsender.h"
#include "util/paramsprovidermerger.h"

const Utf8StringSet ParametrizedUdpSender::supportedParamNames {
  "connecttimeout", "disconnecttimeout", "payload" };

ParametrizedUdpSender::ParametrizedUdpSender(
    QObject *parent, const Utf8String &url, const ParamSet &params,
    ParamsProvider *paramsEvaluationContext, const Utf8String &logTask,
    quint64 logExecId)
  : QUdpSocket(parent) {
  init(url, params, paramsEvaluationContext, logTask, logExecId);
}

ParametrizedUdpSender::ParametrizedUdpSender(
    const Utf8String &url, const ParamSet &params,
    ParamsProvider *paramsEvaluationContext,
    const Utf8String &logTask, quint64 logExecId)
  : QUdpSocket() {
  init(url, params, paramsEvaluationContext, logTask, logExecId);
}

void ParametrizedUdpSender::init(
    const Utf8String &url, const ParamSet &params,
    ParamsProvider *paramsEvaluationContext, const Utf8String &logTask,
    quint64 logExecId) {
  auto ppm = ParamsProviderMerger(params)(paramsEvaluationContext);
  QUrl qurl(PercentEvaluator::eval_utf16(url, &ppm));
  if (qurl.isValid()) {
    _host = qurl.host();
    _port = (quint16)qurl.port(0);
  } else {
    _port = 0;
  }
  _connectTimeout = params.paramNumber<double>(
                      "connecttimeout", 2.0, paramsEvaluationContext)*1000;
  _disconnectTimeout = params.paramNumber<double>(
                      "disconnecttimeout", .2, paramsEvaluationContext)*1000;
  _payloadFromParams = params.paramRawUtf8("payload");
  _logTask = logTask;
  _logExecId = logExecId ? QString::number(logExecId) : QString();
  _params = params;
}

/** @param payload if not set, use "payload" parameter content instead
 * @return false if the request cannot be performed, e.g. unknown address */
bool ParametrizedUdpSender::performRequest(
    const Utf8String &payload, ParamsProvider *payloadEvaluationContext) {
  qint64 rc = -1;
  auto effective_payload = payload.isNull() ? _payloadFromParams : payload;
  auto ppm = ParamsProviderMerger(payloadEvaluationContext)(_params);
  effective_payload = PercentEvaluator::eval_utf8(effective_payload, &ppm);
  if (_host.isEmpty() || !_port)
    return false;
  connectToHost(_host, _port, QIODevice::WriteOnly);
  if (waitForConnected(_connectTimeout)) {
    rc = write(effective_payload);
    if (rc != effective_payload.size())
      Log::warning(_logTask, _logExecId)
          << "error when emiting UDP alert: " << error() << " "
          << errorString();
    //else
    //  Log::debug(_logTask, _logExecId)
    //      << "UDP packet sent on " << _host << ":" << _port << " for "
    //      << rc << " bytes: " << payload.size();
  } else {
    Log::warning(_logTask, _logExecId)
        << "timeout when emiting UDP alert: " << error() << " "
        << errorString();
  }
  disconnectFromHost();
  while(state() != QAbstractSocket::UnconnectedState
        && waitForBytesWritten(_disconnectTimeout))
    ;
  return rc >= 0;
}
