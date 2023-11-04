/* Copyright 2013-2023 Hallowyn and others.
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
  QString _message;
  Log::Severity _severity;
  LogActionData(QString logMessage = QString(),
               Log::Severity severity = Log::Info)
    : _message(logMessage), _severity(severity) { }
  Utf8String toString() const override {
    return "log{ "+Log::severityToString(_severity)+" "+_message+" }";
  }
  Utf8String actionType() const override {
    return "log"_u8;
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    if (instance.isNull())
      Log::log(_severity) << PercentEvaluator::eval_utf8(_message, context);
    else
      Log::log(_severity, instance.taskId(), instance.idAsLong())
          << PercentEvaluator::eval_utf8(_message, context);
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _message);
    node.appendChild(PfNode("severity", Log::severityToString(_severity)));
    return node;
  }
};

LogAction::LogAction(Scheduler *scheduler, PfNode node)
  : Action(new LogActionData(
             node.contentAsUtf16(),
             Log::severityFromString(
               node.utf16attribute("severity", "info").toUtf8()))) {
  Q_UNUSED(scheduler)
}

LogAction::LogAction(const LogAction &rhs) : Action(rhs) {
}

LogAction::~LogAction() {
}
