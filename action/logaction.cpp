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
#include "logaction.h"
#include "action_p.h"

class LogActionData : public ActionData {
public:
  Utf8String _message;
  p6::log::Severity _severity;

  LogActionData(const Utf8String &message = {},
               p6::log::Severity severity = p6::log::Info)
    : _message(message), _severity(severity) { }
  Utf8String toString() const override {
    return "log{ "+p6::log::severity_as_text(_severity)+" "+_message+" }";
  }
  Utf8String actionType() const override {
    return "log"_u8;
  }
  void trigger(EventSubscription sub, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    p6::log::log(_severity, instance.taskId(), instance.id(),
                 "logaction:"_u8+sub.eventName()) << _message % context;
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _message);
    node.append_child(PfNode("severity", p6::log::severity_as_text(_severity)));
    return node;
  }
};

LogAction::LogAction(Scheduler *scheduler, const PfNode &node)
  : Action(new LogActionData(
             node.content_as_text(),
             p6::log::severity_from_text(
               node.attribute("severity", "info")))) {
  Q_UNUSED(scheduler)
}

LogAction::LogAction(const LogAction &rhs) : Action(rhs) {
}

LogAction::~LogAction() {
}
