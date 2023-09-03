/* Copyright 2022 Gregoire Barbier and others.
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
#ifndef TASKWAITCONDITION_H
#define TASKWAITCONDITION_H

#include "condition.h"
#include "pf/pfnode.h"

class TaskWaitConditionData;

enum TaskWaitOperator {
  AllFinished, AnyFinished,
  AllSuccess, AllFailure, AllCanceled,
  NoSuccess, NoFailure, NoCanceled,
  AnySuccess, AnyFailure, AnyCanceled,
  AnyNonSuccess, AnyNonFailure, AnyNonCanceled,
  AllFinishedAnySuccess, AllFinishedAnyFailure, AllFinishedAnyCanceled,
  AllStarted, AnyStarted,
  IsEmpty, IsNotEmpty, True, False,
  UnknownOperator,
};

/** Condition for waiting after other tasks in the same herd. */
class LIBQRONSHARED_EXPORT TaskWaitCondition : public Condition {
public: 
  explicit TaskWaitCondition(PfNode node = {});
  TaskWaitCondition(TaskWaitOperator op, QString expr);
  TaskWaitCondition(const TaskWaitCondition &other);
  ~TaskWaitCondition();
  TaskWaitOperator op() const;
  QString expr() const;
  static TaskWaitOperator operatorFromString(QString op);
  static QString operatorAsString(TaskWaitOperator op);
  static TaskWaitOperator cancelOperatorFromQueueOperator(TaskWaitOperator op);
};

Q_DECLARE_TYPEINFO(TaskWaitCondition, Q_MOVABLE_TYPE);

#endif // TASKWAITCONDITION_H
