/* Copyright 2013 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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
#include "util/paramset.h"

class LogActionData : public ActionData {
public:
  QString _message;
  Log::Severity _severity;
  LogActionData(QString logMessage = QString(),
               Log::Severity severity = Log::Info)
    : _message(logMessage), _severity(severity) { }
  QString toString() const {
    return "log{"+Log::severityToString(_severity)+" "+_message+"}";
  }
  QString actionType() const {
    return "log";
  }
  void trigger(EventSubscription subscription,
               const ParamsProvider *context) const {
    Q_UNUSED(subscription)
    QString fqtn = context
        ? context->paramValue("!fqtn").toString() : QString();
    QString id = context
        ? context->paramValue("!taskrequestid").toString() : QString();
    Log::log(_severity, fqtn, id.toLongLong())
        << ParamSet().evaluate(_message, context);
  }
};

LogAction::LogAction(Log::Severity severity, QString message)
  : Action(new LogActionData(message, severity)) {
}

LogAction::LogAction(const LogAction &rhs) : Action(rhs) {
}

LogAction::~LogAction() {
}
