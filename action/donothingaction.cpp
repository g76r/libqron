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
#include "donothingaction.h"
#include "action_p.h"

class EventSubscription;
class ParamsProviderMerger;
class TaskInstance;

class DoNothingActionData : public ActionData {
  QString _actionType;

public:

  DoNothingActionData(QString actionType = "donothing")
      : _actionType(actionType) { }
  void trigger(EventSubscription, ParamsProviderMerger*,
               TaskInstance) const override {
  }
  Utf8String toString() const override {
    return _actionType;
  }
  Utf8String actionType() const override {
    return _actionType;
  }
  PfNode toPfNode() const override {
    return PfNode(_actionType);
  }
};

DoNothingAction::DoNothingAction(const QString &actionType)
    : Action(new DoNothingActionData(actionType)) {
}

DoNothingAction::~DoNothingAction() {
}
