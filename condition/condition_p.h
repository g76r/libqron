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
#ifndef CONDITION_P_H
#define CONDITION_P_H

#include "condition.h"
#include "sched/taskinstance.h"

class LIBQRONSHARED_EXPORT ConditionData : public QSharedData {
public:
  explicit ConditionData() { }
  virtual ~ConditionData();
  virtual QString toString() const;
  virtual QString conditionType() const;
  virtual bool evaluate(TaskInstance instance, TaskInstance herder,
                        QSet<TaskInstance> herdedTasks) const;
  virtual PfNode toPfNode() const;
  virtual bool isEmpty() const;
};

#endif // CONDITION_P_H
