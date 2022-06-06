/* Copyright 2013-2022 Hallowyn and others.
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
  QString _alert;
  EmitAlertActionData(Scheduler *scheduler = 0, QString alert = QString())
    : ActionData(scheduler), _alert(alert) { }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance) const override {
    if (!_scheduler)
      return;
    if (_alert.isEmpty())
      return;
    _scheduler->alerter()->emitAlert(ParamSet().evaluate(_alert, context));
  }
  QString toString() const override {
    return "!^"+_alert;
  }
  QString actionType() const override {
    return QStringLiteral("emitalert");
  }
  QString targetName() const override {
    return _alert;
  }
  PfNode toPfNode() const override {
    return PfNode(actionType(), _alert);
  }
};

EmitAlertAction::EmitAlertAction(Scheduler *scheduler, PfNode node)
  : Action(new EmitAlertActionData(scheduler, node.contentAsString())) {
}

EmitAlertAction::EmitAlertAction(const EmitAlertAction &rhs) : Action(rhs) {
}

EmitAlertAction::~EmitAlertAction() {
}
