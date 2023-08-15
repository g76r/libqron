/* Copyright 2022-2023 Gregoire Barbier and others.
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
#include "condition_p.h"
#include "taskwaitcondition.h"

Condition::Condition() {
}

Condition::Condition(const Condition &rhs) : d(rhs.d) {
}

Condition::Condition(ConditionData *data) : d(data) {
}

Condition &Condition::operator=(const Condition &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

Condition::~Condition() {
}

ConditionData::~ConditionData() {
}

QString ConditionData::toString() const {
  return "Condition"; // should never happen
}

QString ConditionData::conditionType() const {
  return "unknown"; // should never happen
}

bool ConditionData::evaluate(TaskInstance, TaskInstance,
                        QSet<TaskInstance>) const {
  return false; // should never happen
}

PfNode ConditionData::toPfNode() const {
  return PfNode(); // should never happen
}

bool ConditionData::isEmpty() const {
  return true; // right: a Condition() is an empty condition
}

QString Condition::toString() const {
  return d ? d->toString() : QString();
}

QString Condition::conditionType() const {
  return d ? d->conditionType() : QString();
}

bool Condition::isEmpty() const {
  return d ? d->isEmpty() : true;
}

bool Condition::evaluate(TaskInstance instance, TaskInstance herder,
  QSet<TaskInstance> herdedTasks) const {
  return d ? d->evaluate(instance, herder, herdedTasks) : false;
}

PfNode Condition::toPfNode() const {
  return d ? d->toPfNode() : PfNode();
}

Condition Condition::createCondition(PfNode node) {
  Condition condition = TaskWaitCondition(node);
  if (condition.isNull()) {
    Log::error() << "unknown condition type: " << node.name();
  }
  return condition;
}
