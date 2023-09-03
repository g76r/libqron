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
#ifndef PARAMETRIZEDNETWORKREQUEST_H
#define PARAMETRIZEDNETWORKREQUEST_H

#include "libqron_global.h"
#include "util/paramset.h"
#include "httpd/httprequest.h"
#include "util/utf8stringset.h"
#include <QNetworkRequest>
#include <QNetworkAccessManager>

/** Class extending QNetworkRequest to give easy ways to parametrize the
 * request using ParamSet parameters.
 * Supported parameters:
 * - "method" to set HTTP method (default: GET)
 * - "user" and "password" to set HTTP basic authentication
 * - "proto" to set network protocol (default: http)
 * - "port" to set TCP port number (overrinding the one specified in the url)
 * - "payload" to set the request payload/body (ignored if performRequest()
 *   is called with a non-null payload)
 * - "content-type" to set payload (and header) content type
 * - "follow-redirect" if true allows following redirect (default: false)
 *       see QNetworkRequest::FollowRedirectsAttribute
 *       not implemented on Qt < 5.6.0
 * - "redirect-max" if > 0 allows following redirect, using choosen max
 *       see QNetworkRequest::setMaximumRedirectsAllowed()
 */
class LIBQRONSHARED_EXPORT ParametrizedNetworkRequest : public QNetworkRequest {
  Utf8String _logTask, _logExecId;
  HttpRequest::HttpMethod _method;
  Utf8String _rawPayloadFromParams;
  ParamSet _params;

public:
  /**
   * @param logTask only used in log, e.g. task id
   * @param logExecId only used in log, e.g. task instance id
   */
  ParametrizedNetworkRequest(
      const Utf8String &url, const ParamSet &params,
      const ParamsProvider *paramsEvaluationContext = 0,
      const Utf8String &logTask = {}, quint64 logExecId = 0);
  /** @param payload if not set, use "payload" parameter content instead
   * @return 0 if the request cannot be performed, e.g. unknown method */
  QNetworkReply *performRequest(
      QNetworkAccessManager *nam, const Utf8String &payload = {},
      const ParamsProvider *payloadEvaluationContext = 0);
  const static Utf8StringSet supportedParamNames;
};

#endif // PARAMETRIZEDNETWORKREQUEST_H
