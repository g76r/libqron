/* Copyright 2022-2024 Gregoire Barbier and others.
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
#ifndef CONDITION_H
#define CONDITION_H

#include "libqron_global.h"
#include "pf/pfnode.h"
#include <QSharedDataPointer>

class ConditionData;
class TaskInstance;

/** Runtime condition in the context of a task instance, e.g. a set of
 * other tasks of same herd has finished, or a timeout is over. */
class LIBQRONSHARED_EXPORT Condition {
protected:
  QSharedDataPointer<ConditionData> d;

public:
  Condition();
  Condition(const Condition &other);
  Condition &operator=(const Condition &other);
  ~Condition();
  bool isNull() const { return !d; }
  bool isEmpty() const;
  bool evaluate(TaskInstance instance, TaskInstance herder,
                QSet<TaskInstance> herdedTasks) const;
  /** Human readable description of action */
  Utf8String toString() const;
  /** Type of action for programmatic test, e.g. "anyfinished" */
  Utf8String conditionType() const;
  PfNode toPfNode() const;
  /** Create the appropriate Condition subclass according to configuration
   *  fragment. */
  static Condition createCondition(PfNode node);

protected:
  explicit Condition(ConditionData *data);
};

Q_DECLARE_TYPEINFO(Condition, Q_MOVABLE_TYPE);

#endif // CONDITION_H
