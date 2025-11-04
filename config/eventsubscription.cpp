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
#include "eventsubscription.h"
#include "task.h"
#include "sched/taskinstance.h"
#include "action/action.h"
#include "configutils.h"
#include "sched/scheduler.h"

class EventSubscriptionData : public QSharedData {
public:
  QString _subscriberName, _eventName;
  QRegularExpression _filter;
  QList<Action> _actions;
  ParamSet _globalParams;
  EventSubscriptionData(const QString &subscriberName = {},
                        const QString &eventName = {},
                        const ParamSet &globalParams = {})
    : _subscriberName(subscriberName), _eventName(eventName),
      _globalParams(globalParams) { }
};

EventSubscription::EventSubscription() {
}

EventSubscription::EventSubscription(
    const QString &subscriberName, const PfNode &node, Scheduler *scheduler,
    const QStringList &ignoredChildren)
  : d(new EventSubscriptionData(subscriberName)) {
  d->_eventName = node.name();
  d->_filter = QRegularExpression(node.content_as_text());
  if (scheduler)
    d->_globalParams = scheduler->globalParams();
  for (const PfNode &child: node.children()) {
    if (child^ignoredChildren)
      continue;
    Action a = Action::createAction(child, scheduler);
    if (!a.isNull())
      d->_actions.append(a);
  }
}

EventSubscription::EventSubscription(
    const QString &subscriberName, const QString &eventName,
    const Action &action)
  : d(new EventSubscriptionData(subscriberName, eventName)) {
  d->_actions.append(action);
}

EventSubscription::EventSubscription(
    const QString &subscriberName, const QString &eventName,
    const QList<Action> &actions)
  : d(new EventSubscriptionData(subscriberName, eventName)) {
  d->_actions = actions;
}

EventSubscription::~EventSubscription() {
}

EventSubscription::EventSubscription(const EventSubscription &rhs) : d(rhs.d) {
}

EventSubscription &EventSubscription::operator=(const EventSubscription &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

bool EventSubscription::triggerActions(
    ParamsProviderMerger *context, TaskInstance instance,
    std::function<bool(Action a)> filter) const {
  if (!d)
    return false;
  bool stopped = false;
  if (!!instance)
    context->append(&instance);
  for (const Action &a: d->_actions) {
    if (a.actionType() == "stop"_u8) {
      stopped = true;
      break;
    }
    if (!filter || filter(a))
      a.trigger(*this, context, instance);
  }
  if (!!instance)
    context->pop_back();
  return stopped;
}

Utf8StringList EventSubscription::toStringList(
    const QList<EventSubscription> &list) {
  Utf8StringList l;
  for (const auto &sub: list)
    l.append(sub.toStringList());
  return l;
}

Utf8StringList EventSubscription::toStringList() const {
  return Action::toStringList(actions());
}

QString EventSubscription::eventName() const {
  return d ? d->_eventName : QString();
}

QList<Action> EventSubscription::actions() const {
  return d ? d->_actions : QList<Action>();
}

QString EventSubscription::subscriberName() const {
  return d ? d->_subscriberName : QString();
}

void EventSubscription::setSubscriberName(const QString &name) {
  if (d)
    d->_subscriberName = name;
}

QRegularExpression EventSubscription::filter() const {
  return d ? d->_filter : QRegularExpression();
}

PfNode EventSubscription::toPfNode() const {
  if (!d)
    return PfNode();
  PfNode node(d->_eventName, d->_filter.pattern());
  for (const Action &action: d->_actions)
    node.append_child(action.toPfNode());
  return node;
}
