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
#include "trigger_p.h"
#include "config/configutils.h"

Trigger::Trigger() {
}

Trigger::Trigger(const Trigger &rhs) : d(rhs.d) {
}

Trigger::Trigger(TriggerData *data) : d(data) {
}

Trigger &Trigger::operator=(const Trigger &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

Trigger::~Trigger() {
}

Utf8String Trigger::expression() const {
  return d ? d->expression() : Utf8String{};
}

Utf8String Trigger::canonicalExpression() const {
  return d ? d->canonicalExpression() : Utf8String{};
}

Utf8String Trigger::humanReadableExpression() const {
  return d ? d->humanReadableExpression() : Utf8String{};
}

Utf8String Trigger::humanReadableExpressionWithCalendar() const {
  if (calendar().isNull())
    return humanReadableExpression();
  Utf8String cal = calendar().toPfNode(true)
                   .as_pf(PfOptions().with_payload_first());
  return "["+cal+"]"+humanReadableExpression();
}

bool Trigger::isValid() const {
  return d && d->isValid();
}

TriggerData::~TriggerData() {
}

Utf8String TriggerData::expression() const {
  return {};
}

Utf8String TriggerData::canonicalExpression() const {
  return expression();
}

Utf8String TriggerData::humanReadableExpression() const {
  return expression();
}

bool TriggerData::isValid() const {
  return false;
}

//void Trigger::setCalendar(Calendar calendar) {
//  if (d)
//    d->_calendar = calendar;
//}

Calendar Trigger::calendar() const {
  return d ? d->_calendar : Calendar();
}

ParamSet Trigger::overridingParams() const {
  return d ? d->_overridingParams : ParamSet();
}

bool Trigger::loadConfig(
    const PfNode &node, const QMap<Utf8String,Calendar> &namedCalendars) {
  auto [child,unwanted] = node.first_two_children("calendar");
  if (!!unwanted) {
    Log::error() << "multiple calendar definition, ignoring all of them: "
                 << node.as_pf();
  } else if (!!child) {
    auto content = child.content_as_text();
    if (!content.isEmpty()) {
      Calendar calendar = namedCalendars.value(content);
      if (calendar.isNull())
        Log::error() << "ignoring undefined calendar '" << content
                     << "': " << child.as_pf();
      else
        d->_calendar = calendar;
    } else {
      Calendar calendar = Calendar(child);
      if (calendar.isNull())
        Log::error() << "ignoring empty calendar: "
                       << child.as_pf();
      else
        d->_calendar = calendar;
    }
  }
  d->_overridingParams = ParamSet(node, "param");
  return true;
}

Utf8String TriggerData::triggerType() const {
  return "unknown"_u8;
}

PfNode TriggerData::toPfNode() const {
  PfNode node(triggerType(), expression());
  ConfigUtils::writeParamSet(&node, _overridingParams, "param");
  if (!_calendar.isNull())
    node.append_child(_calendar.toPfNode(true));
  return node;
}

PfNode Trigger::toPfNode() const {
  return d ? d->toPfNode() : PfNode();
}
