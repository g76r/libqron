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
#ifndef PLANTASKACTION_H
#define PLANTASKACTION_H

#include "action.h"

class PlanTaskActionData;
class Scheduler;
class DisjunctionCondition;

/** Action planning a task execution for queuing when a condition will be
 * reached.
 * Id parameter can be either fully qualified ("group.shortid") or short and
 * local to caller task group if any, which is usefull if this action has been
 * triggered by a task onsuccess/onfailure/onfinish event.
 * In addition id parameter is evaluated within event context.
 */
class LIBQRONSHARED_EXPORT PlanTaskAction : public Action {
public:
  PlanTaskAction();
  explicit PlanTaskAction(Scheduler *scheduler = 0, PfNode node = PfNode());
  PlanTaskAction(Scheduler *scheduler, QByteArray taskId);
  PlanTaskAction(const PlanTaskAction &);
  ~PlanTaskAction();
  DisjunctionCondition queuewhen() const;
  DisjunctionCondition cancelwhen() const;
};

Q_DECLARE_TYPEINFO(PlanTaskAction, Q_RELOCATABLE_TYPE);

#endif // PLANTASKACTION_H
