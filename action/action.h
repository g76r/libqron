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
#ifndef ACTION_H
#define ACTION_H

#include "libqron_global.h"
#include <QSharedDataPointer>
#include "pf/pfnode.h"
#include "util/paramsprovidermerger.h"

class ActionData;
class EventSubscription;
class Scheduler;
class TaskInstance;

/** Action performed when an event (e.g. onsuccess) occurs. */
class LIBQRONSHARED_EXPORT Action {
protected:
  QSharedDataPointer<ActionData> d;

public:
  Action();
  Action(const Action &);
  Action &operator=(const Action &);
  ~Action();
  bool isNull() const { return !d; }
  void trigger(EventSubscription subscription, ParamsProviderMerger *context,
               TaskInstance instance) const;
  /** Human readable description of action */
  Utf8String toString() const;
  /** Type of action for programmatic test, e.g. "postnotice" */
  Utf8String actionType() const;
  /** Action can create task instances, e.g. "requesttask" */
  bool mayCreateTaskInstances() const;
  static Utf8StringList toStringList(QList<Action> list);
  /** Name of the target, for actions where it makes sense. Null otherwise.
   * e.g. task id for requesttask, notice for postnotice, etc. */
  Utf8String targetName() const;
  ParamSet params() const;
  PfNode toPfNode() const;
  /** Create the appropriate Action subclass according to configuration
   *  fragment. */
  static Action createAction(const PfNode &node, Scheduler *scheduler);

protected:
  explicit Action(ActionData *data);
};

Q_DECLARE_TYPEINFO(Action, Q_MOVABLE_TYPE);

#endif // ACTION_H
