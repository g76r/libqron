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
#ifndef EVENTSUBSCRIPTION_H
#define EVENTSUBSCRIPTION_H

#include "libqron_global.h"

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
  EventSubscription(QString subscriberName, QString eventName, Action action);
  EventSubscription(QString subscriberName, QString eventName,
                    QList<Action> actions);
  /** Parse configuration fragment. */
  EventSubscription(QString subscriberName, PfNode node, Scheduler *scheduler,
                    QStringList ignoredChildren = QStringList());
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
  void setSubscriberName(QString name);
  /** Filter expression, if any.
   * e.g. path and file pattern for onfilexxx events */
  QRegularExpression filter() const;
  /** Trigger actions if context complies with filter conditions.
   * Use this method if the event occured in the context of a task.
   * @param eventContext will get taskContext::params() as parent before being
   * passed to Action::trigger()
   * @return true if stop action was encountered */
  [[nodiscard]] bool triggerActions(
      ParamsProviderMerger *context, TaskInstance instance,
      std::function<bool(Action a)> filter) const;
  /** Syntaxic sugar */
  [[nodiscard]] bool triggerActions(
      ParamsProviderMerger *context, TaskInstance instance) const;
  /** Syntaxic sugar */
  [[nodiscard]] bool triggerActions() const;
  static QStringList toStringList(QList<EventSubscription> list);
  QStringList toStringList() const;
  PfNode toPfNode() const;
};

Q_DECLARE_METATYPE(EventSubscription)
Q_DECLARE_TYPEINFO(EventSubscription, Q_MOVABLE_TYPE);

#endif // EVENTSUBSCRIPTION_H
