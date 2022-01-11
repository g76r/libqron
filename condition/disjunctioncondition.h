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
#ifndef DISJUNCTIONCONDITION_H
#define DISJUNCTIONCONDITION_H

#include "condition.h"

class LIBQRONSHARED_EXPORT DisjunctionCondition : public Condition {
public:
  DisjunctionCondition();
  DisjunctionCondition(std::initializer_list<Condition> conditions);
  DisjunctionCondition(QList<PfNode> nodes);
  DisjunctionCondition(const DisjunctionCondition&);
  ~DisjunctionCondition();
  void append(QList<PfNode> nodes);
  QList<Condition> conditions() const;
  operator QList<Condition>() const { return conditions(); }
  QList<PfNode> toPfNodes() const;
  bool isEmpty() const { return conditions().isEmpty(); }
  int size() const { return conditions().size(); }
};

Q_DECLARE_TYPEINFO(DisjunctionCondition, Q_MOVABLE_TYPE);

#endif // DISJUNCTIONCONDITION_H

