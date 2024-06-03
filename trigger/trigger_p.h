/* Copyright 2013-2024 Hallowyn and others.
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

#ifndef TRIGGER_P_H
#define TRIGGER_P_H

#include "trigger.h"
#include "util/paramset.h"

class LIBQRONSHARED_EXPORT TriggerData : public QSharedData {
public:
  ParamSet _overridingParams;
  Calendar _calendar;
  Utf8StringList _commentsList;
  virtual Utf8String expression() const;
  /** default: call expression() */
  virtual Utf8String canonicalExpression() const;
  /** default: call expression() */
  virtual Utf8String humanReadableExpression() const;
  virtual bool isValid() const;
  virtual ~TriggerData();
  /** Type of trigger for programmatic test, e.g. "cron" */
  virtual Utf8String triggerType() const;
  PfNode toPfNode() const;
};

#endif // TRIGGER_P_H
