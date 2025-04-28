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
#include "emitalertaction.h"
#include "action_p.h"
#include "alert/alerter.h"

class EmitAlertActionData : public ActionData {
public:
  Utf8String _alert;
  EmitAlertActionData(Scheduler *scheduler = 0, const Utf8String &alert = {})
    : ActionData(scheduler), _alert(alert) { }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance) const override {
    if (!_scheduler)
      return;
    if (_alert.isEmpty())
      return;
    _scheduler->alerter()->emitAlert(
          PercentEvaluator::eval_utf8(_alert, context));
  }
  Utf8String toString() const override {
    return "!^"+_alert;
  }
  Utf8String actionType() const override {
    return "emitalert"_u8;
  }
  Utf8String targetName() const override {
    return _alert;
  }
  PfNode toPfNode() const override {
    return PfNode(actionType(), _alert);
  }
};

EmitAlertAction::EmitAlertAction(Scheduler *scheduler, const PfNode &node)
  : Action(new EmitAlertActionData(scheduler, node.content_as_text())) {
}

EmitAlertAction::EmitAlertAction(const EmitAlertAction &rhs) : Action(rhs) {
}

EmitAlertAction::~EmitAlertAction() {
}
