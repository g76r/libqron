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
#include "raisealertaction.h"
#include "action_p.h"
#include "alert/alerter.h"

class RaiseAlertActionData : public ActionData {
public:
  QString _alert;
  RaiseAlertActionData(Scheduler *scheduler = 0, QString alert = QString())
    : ActionData(scheduler), _alert(alert) { }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance) const override {
    if (!_scheduler)
      return;
    _scheduler->alerter()->raiseAlert(
          PercentEvaluator::eval_utf8(_alert, context));
  }
  Utf8String toString() const override {
    return "!+"+_alert;
  }
  Utf8String actionType() const override {
    return "raisealert"_u8;
  }
  Utf8String targetName() const override {
    return _alert;
  }
  PfNode toPfNode() const override {
    return PfNode(actionType(), _alert);
  }
};

RaiseAlertAction::RaiseAlertAction(Scheduler *scheduler, const PfNode &node)
  : Action(new RaiseAlertActionData(scheduler, node.content_as_text())) {
}

RaiseAlertAction::RaiseAlertAction(const RaiseAlertAction &rhs) : Action(rhs) {
}

RaiseAlertAction::~RaiseAlertAction() {
}
