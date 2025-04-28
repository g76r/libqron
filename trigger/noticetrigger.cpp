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
#include "noticetrigger.h"
#include "trigger_p.h"
#include "pf/pfnode.h"

class NoticeTriggerData : public TriggerData {
public:
  Utf8String _notice;
  NoticeTriggerData(const Utf8String &notice = {}) : _notice(notice) { }
  Utf8String expression() const override { return _notice; }
  Utf8String humanReadableExpression() const override { return "^"+_notice; }
  bool isValid() const override { return !_notice.isEmpty(); }
  Utf8String triggerType() const override { return "notice"_u8; }
};

NoticeTrigger::NoticeTrigger() {
}

NoticeTrigger::NoticeTrigger(const PfNode &node,
                             const QMap<Utf8String, Calendar> &namedCalendars)
  : Trigger(new NoticeTriggerData(node.content_as_text())){
  loadConfig(node, namedCalendars);
}


NoticeTrigger::NoticeTrigger(const NoticeTrigger &rhs) : Trigger(rhs) {
}

NoticeTrigger &NoticeTrigger::operator=(const NoticeTrigger &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

NoticeTrigger::~NoticeTrigger() {
}
