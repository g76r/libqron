/* Copyright 2013 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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
#ifndef HTTPACTION_H
#define HTTPACTION_H

#include "action.h"
#include "util/paramset.h"

class HttpActionData;

/** Action sending an arbitrary HTTP request.
 * @deprecated not yet implemented */
class HttpAction : public Action{
public:
  explicit  HttpAction(QString url = QString(), ParamSet params = ParamSet());
  HttpAction(const HttpAction &);
  ~HttpAction();
};

#endif // HTTPACTION_H
