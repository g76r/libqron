/* Copyright 2022-2024 Gregoire Barbier and others.
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
#ifndef OVERRIDEPARAMACTION_H
#define OVERRIDEPARAMACTION_H

#include "action.h"

class Scheduler;

class LIBQRONSHARED_EXPORT OverrideParamAction: public Action {
public:
  OverrideParamAction();
  explicit OverrideParamAction(
    Scheduler *scheduler = 0, PfNode node = PfNode());
  OverrideParamAction(const OverrideParamAction &);
  ~OverrideParamAction();
};

Q_DECLARE_TYPEINFO(OverrideParamAction, Q_RELOCATABLE_TYPE);

#endif // OVERRIDEPARAMACTION_H
