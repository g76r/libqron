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
#include "disjunctioncondition.h"
#include "condition_p.h"

class DisjunctionConditionData : public ConditionData {
public:
  QList<Condition> _conditions;

  DisjunctionConditionData(
      QList<Condition> conditions = QList<Condition>())
    : _conditions(conditions) { }
  QString toString() const override {
    QString s = "anyof {";
    for (auto c: _conditions)
      s += ' ' + c.toString();
    s += " }";
    return s;
  }
  QString conditionType() const override { return "disjunction"; }
  bool evaluate(TaskInstance instance, TaskInstance herder,
                QSet<TaskInstance> herdedTasks) const override {
    if (_conditions.isEmpty())
      return true;
    for (auto condition: _conditions) {
      if (condition.evaluate(instance, herder, herdedTasks))
        return true;
    }
    return false;
  }
  bool isEmpty() const override {
    return _conditions.isEmpty();
  }
  PfNode toPfNode() const override {
    return PfNode();
  }
};

DisjunctionCondition::DisjunctionCondition() {
}

DisjunctionCondition::DisjunctionCondition(
    std::initializer_list<Condition> conditions)
  : Condition(new DisjunctionConditionData(conditions)) {
}
DisjunctionCondition::DisjunctionCondition(const DisjunctionCondition &other)
  : Condition(other) {
}

DisjunctionCondition::DisjunctionCondition(QList<PfNode> nodes)
    : Condition(new DisjunctionConditionData) {
  append(nodes);
}

DisjunctionCondition::~DisjunctionCondition() {
}

void DisjunctionCondition::append(QList<PfNode> nodes) {
  auto data = static_cast<DisjunctionConditionData*>(d.data());
  if (!data)
    return;
  for (auto node: nodes) {
    Condition c = Condition::createCondition(node);
    if (c.isNull())
      continue;
    data->_conditions.append(c);
  }
}

QList<Condition> DisjunctionCondition::conditions() const {
  auto data = static_cast<const DisjunctionConditionData*>(d.data());
  return data ? data->_conditions : QList<Condition>();
}

QList<PfNode> DisjunctionCondition::toPfNodes() const {
  QList<PfNode> nodes;
  for (auto c: conditions())
    nodes.append(c.toPfNode());
  return nodes;
}
