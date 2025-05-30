/* Copyright 2022-2025 Gregoire Barbier and others.
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
#ifndef DONOTHINGACTION_H
#define DONOTHINGACTION_H

#include "action.h"

class LIBQRONSHARED_EXPORT DoNothingAction : public Action {
public:
  DoNothingAction(const QString &actionType);
  DoNothingAction(const DoNothingAction &other)
      : DoNothingAction(other.actionType()) { }
  ~DoNothingAction();
};

Q_DECLARE_TYPEINFO(DoNothingAction, Q_RELOCATABLE_TYPE);

#endif // DONOTHINGACTION_H
