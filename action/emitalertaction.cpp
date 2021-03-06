/* Copyright 2013-2016 Hallowyn and others.
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

class EmitAlertActionData : public ActionData {
public:
  QString _alert;
  EmitAlertActionData(Scheduler *scheduler = 0, QString alert = QString())
    : ActionData(scheduler), _alert(alert) { }
  void trigger(EventSubscription subscription, ParamSet eventContext,
               TaskInstance instance) const {
    Q_UNUSED(subscription)
    if (_scheduler && !_alert.isEmpty()) {
      TaskInstancePseudoParamsProvider ppp = instance.pseudoParams();
      _scheduler->alerter()->emitAlert(eventContext.evaluate(_alert, &ppp));
    }
  }
  QString toString() const {
    return "!^"+_alert;
  }
  QString actionType() const {
    return QStringLiteral("emitalert");
  }
  QString targetName() const {
    return _alert;
  }
  PfNode toPfNode() const{
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
