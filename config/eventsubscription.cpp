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
  Utf8StringList _commentsList;
  EventSubscriptionData(QString subscriberName = QString(),
                        QString eventName = QString(),
                        ParamSet globalParams = ParamSet())
    : _subscriberName(subscriberName), _eventName(eventName),
      _globalParams(globalParams) { }
};

EventSubscription::EventSubscription() {
}

EventSubscription::EventSubscription(
    QString subscriberName, PfNode node, Scheduler *scheduler,
    QStringList ignoredChildren)
  : d(new EventSubscriptionData(subscriberName)) {
  d->_eventName = node.name();
  d->_filter = QRegularExpression(node.contentAsUtf16());
  if (scheduler)
    d->_globalParams = scheduler->globalParams();
  foreach (PfNode child, node.children()) {
    if (ignoredChildren.contains(child.name()) || child.isComment())
      continue;
    Action a = Action::createAction(child, scheduler);
    if (!a.isNull())
      d->_actions.append(a);
  }
  ConfigUtils::loadComments(node, &d->_commentsList, 0);
}

EventSubscription::EventSubscription(
    QString subscriberName, QString eventName, Action action)
  : d(new EventSubscriptionData(subscriberName, eventName)) {
  d->_actions.append(action);
}

EventSubscription::EventSubscription(
    QString subscriberName, QString eventName, QList<Action> actions)
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
  ParamsProviderMergerRestorer ppmr(context);
  if (!instance.isNull())
    context->append(&instance);
  for (Action a: d->_actions) {
    if (a.actionType() == "stop")
      return true;
    if (filter(a))
      a.trigger(*this, context, instance);
  }
  return false;
}

bool EventSubscription::triggerActions(
    ParamsProviderMerger *context, TaskInstance instance) const {
  return triggerActions(context, instance, [](Action){ return true; });
}

bool EventSubscription::triggerActions() const {
  ParamsProviderMerger context;
  return triggerActions(&context, TaskInstance(), [](Action){ return true; });
}

Utf8StringList EventSubscription::toStringList(QList<EventSubscription> list) {
  Utf8StringList l;
  for (auto sub: list)
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

void EventSubscription::setSubscriberName(QString name) {
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
  ConfigUtils::writeComments(&node, d->_commentsList);
  foreach(const Action &action, d->_actions)
    node.appendChild(action.toPfNode());
  return node;
}
