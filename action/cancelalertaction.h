/* Copyright 2013-2025 Hallowyn and others.
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
#ifndef CANCELALERTACTION_H
#define CANCELALERTACTION_H

#include "action.h"

class CancelAlertActionData;
class Scheduler;

/** Action canceling an alert. */
class LIBQRONSHARED_EXPORT CancelAlertAction : public Action {
public:
  explicit CancelAlertAction(Scheduler *scheduler = 0, const PfNode &node = {});
  CancelAlertAction(const CancelAlertAction &);
  ~CancelAlertAction();
};

Q_DECLARE_TYPEINFO(CancelAlertAction, Q_RELOCATABLE_TYPE);

#endif // CANCELALERTACTION_H
