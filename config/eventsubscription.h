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
#ifndef EVENTSUBSCRIPTION_H
#define EVENTSUBSCRIPTION_H

#include "libqron_global.h"
#include <QSharedDataPointer>
#include "util/paramsprovidermerger.h"
#include "sched/taskinstance.h"
#include "action/action.h"

class EventSubscriptionData;
class TaskInstance;
class Task;
class Action;
class Scheduler;

// TODO convert to SharedUiItem

/** Subscription to a given event (e.g. onsuccess) for one or several actions
 * (e.g. emitalert) with an optionnal filter and optionnal event subscription
 * parameters. */
class LIBQRONSHARED_EXPORT EventSubscription {
  QSharedDataPointer<EventSubscriptionData> d;

public:
  EventSubscription();
  EventSubscription(const QString &subscriberName, const QString &eventName,
                    const Action &action);
  EventSubscription(const QString &subscriberName, const QString &eventName,
                    const QList<Action> &actions);
  /** Parse configuration fragment. */
  EventSubscription(
      const QString &subscriberName, const PfNode &node, Scheduler *scheduler,
      const QStringList &ignoredChildren = {});
  EventSubscription(const EventSubscription &);
  EventSubscription &operator=(const EventSubscription &);
  ~EventSubscription();
  bool isNull() const { return !d; }
  QList<Action> actions() const;
  /** Name of the matched event. e.g. "onstart" "onnotice" "onstatus" */
  QString eventName() const;
  /** Name identifiying the subscriber.
   * e.g. taskid for a task, groupid for a group, * for global subscriptions. */
  QString subscriberName() const;
  void setSubscriberName(const QString &name);
  /** Filter expression, if any.
   * e.g. path and file pattern for onfilexxx events */
  QRegularExpression filter() const;
  /** Trigger actions if context complies with filter conditions.
   * Use this method if the event occured in the context of a task.
   * @param eventContext will get taskContext::params() as parent before being
   * passed to Action::trigger()
   * @return true if stop action was encountered */
  [[nodiscard]] bool triggerActions(
      ParamsProviderMerger *context, TaskInstance instance = {},
      std::function<bool(Action a)> filter = {}) const;
  static Utf8StringList toStringList(const QList<EventSubscription> &list);
  Utf8StringList toStringList() const;
  PfNode toPfNode() const;
};

Q_DECLARE_METATYPE(EventSubscription)
Q_DECLARE_TYPEINFO(EventSubscription, Q_MOVABLE_TYPE);

#endif // EVENTSUBSCRIPTION_H
