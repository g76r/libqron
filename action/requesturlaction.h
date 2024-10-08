/* Copyright 2014-2024 Hallowyn and others.
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
#ifndef REQUESTURLACTION_H
#define REQUESTURLACTION_H

#include "action.h"

/** Action requesting an arbitrary URL.
 * e.g. (requesturl(address udp://127.0.0.1:8125)"task.%!taskid.time:%!totalms|ms")
 * @see ParametrizedNetworkRequest
 * @see ParametrizedUdpSender */
class RequestUrlAction : public Action {
public:
  explicit RequestUrlAction(Scheduler *scheduler = 0, PfNode node = PfNode());
  RequestUrlAction(const RequestUrlAction&);
};

Q_DECLARE_TYPEINFO(RequestUrlAction, Q_RELOCATABLE_TYPE);

#endif // REQUESTURLACTION_H
