/* Copyright 2022-2025 Gregoire Barbier and others.
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
#include "overrideparamaction.h"
#include "action_p.h"
#include "sched/taskinstance.h"

class OverrideParamActionData : public ActionData {
public:
  QString _key, _rawvalue;
  OverrideParamActionData(QString key, QString rawvalue)
    : _key(key), _rawvalue(rawvalue) { }
  OverrideParamActionData()
    : OverrideParamActionData(QString(), QString()) { }
  OverrideParamActionData(const std::pair<Utf8String,Utf8String> &pair)
    : OverrideParamActionData(pair.first, pair.second) { }
  Utf8String toString() const override {
    return "overrideparam{ "+_key+" += "+_rawvalue+" }";
  }
  Utf8String actionType() const override {
    return "overrideparam"_u8;
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    if (instance.isNull())
      return;
    auto value = PercentEvaluator::eval_utf8(_rawvalue, context);
    instance.setParam(_key, value);
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _key+" "+_rawvalue);
    return node;
  }
};

OverrideParamAction::OverrideParamAction(Scheduler*, const PfNode &node)
  : Action(new OverrideParamActionData(node.content_as_text_pair())) {
}

OverrideParamAction::OverrideParamAction(const OverrideParamAction &rhs)
  : Action(rhs) {
}

OverrideParamAction::~OverrideParamAction() {
}
