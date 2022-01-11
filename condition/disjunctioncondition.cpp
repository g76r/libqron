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
  QString toString() const override { return "DisjunctionCondition"; }
  QString conditionType() const override { return "or"; }
  bool evaluate(ParamSet eventContext,
                TaskInstance taskContext) const override {
    if (_conditions.isEmpty())
      return true;
    for (auto condition: _conditions) {
      if (condition.evaluate(eventContext, taskContext))
        return true;
    }
    return false;
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

DisjunctionCondition::DisjunctionCondition(QList<PfNode> nodes) {
  appendConditions(nodes);
}

DisjunctionCondition::~DisjunctionCondition() {
}

void DisjunctionCondition::appendConditions(QList<PfNode> nodes) {
  if (!d)
    return;
  auto dd = static_cast<DisjunctionConditionData*>(d.data());
  for (auto child: nodes) {
    Condition c = Condition::createCondition(child);
    if (c.isNull())
      continue;
    dd->_conditions.append(c);
  }
}
