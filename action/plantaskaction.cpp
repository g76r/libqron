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
#include "plantaskaction.h"
#include "action_p.h"
#include "config/configutils.h"
#include "condition/disjunctioncondition.h"

class PlanTaskActionData : public ActionData {
public:
  Utf8String _id;
  ParamSet _overridingParams;
  bool _force, _lone;
  QHash<Utf8String,Utf8String> _paramappend;
  DisjunctionCondition _queuewhen, _cancelwhen;

  PlanTaskActionData(
      Scheduler *scheduler, QString id, ParamSet params, bool force,
      bool lone, QHash<Utf8String,Utf8String> paramappend,
      DisjunctionCondition queuewhen, DisjunctionCondition cancelwhen)
      : ActionData(scheduler), _id(id), _overridingParams(params),
        _force(force), _lone(lone), _paramappend(paramappend),
        _queuewhen(queuewhen), _cancelwhen(cancelwhen) { }
  void trigger(EventSubscription subscription, ParamsProviderMerger *context,
               TaskInstance parentInstance) const override {
    if (!_scheduler)
      return;
    auto id = PercentEvaluator::eval_utf8(_id, context);
    auto parent_taskid = parentInstance.taskId();
    auto last_dot = parent_taskid.lastIndexOf('.');
    auto parent_taskgroupid_dot = parent_taskid.left(last_dot+1);
    auto parent_localid = parent_taskid.mid(last_dot+1);
    if (!parentInstance.isNull()) {
      auto idIfLocalToGroup = parent_taskgroupid_dot+id;
      if (_scheduler->taskExists(idIfLocalToGroup))
        id = idIfLocalToGroup;
    }
    quint64 herdid = _lone ? 0 : parentInstance.herdid();
    ParamSet overridingParams;
    for (auto key: _overridingParams.paramKeys())
      overridingParams.insert(
            key, PercentEvaluator::escape(
              PercentEvaluator::eval(
                _overridingParams.paramRawUtf8(key), context)));
    if (!!parentInstance) {
      overridingParams.insert("!parenttaskid", parentInstance.taskId());
      overridingParams.insert("!parenttasklocalid", parent_localid);
    }
    auto cause = subscription.eventName();
    TaskInstance instance =
        _scheduler->planTask(id, overridingParams, _force, herdid, _queuewhen,
                             _cancelwhen, parentInstance.idAsLong(), cause);
    if (!instance) {
      Log::error(parentInstance.taskId(), parentInstance.idAsLong())
          << "plantask action failed to plan execution of task "
          << id << " within event subscription context "
          << subscription.subscriberName() << "|" << subscription.eventName();
      return;
    }
    Log::info(parentInstance.taskId(), parentInstance.idAsLong())
        << "plantask action planned execution of task "
        << instance.taskId()
        << " with queue condition " << _queuewhen.toString()
        << " and cancel condition " << _cancelwhen.toString();
    if (_paramappend.isEmpty() || !herdid)
      return;
    context->prepend(&instance);
    for (auto [key,rawvalue]: _paramappend.asKeyValueRange()) {
      auto value = PercentEvaluator::eval_utf8(rawvalue, context);
      _scheduler->taskInstanceParamAppend(
            herdid, key, PercentEvaluator::escape(value));
    }
    context->pop_front();
  }
  Utf8String toString() const override {
    return "*" + _id;
  }
  Utf8String actionType() const override {
    return "plantask"_u8;
  }
  bool mayCreateTaskInstances() const override {
    return true;
  }
  Utf8String targetName() const override {
    return _id;
  }
  ParamSet params() const override {
    return _overridingParams;
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _id);
    ConfigUtils::writeParamSet(&node, _overridingParams, "param");
    if (_force)
      node.append_child(PfNode("force"));
    if (_lone)
      node.append_child(PfNode("lone"));
    ConfigUtils::writeParamSet(&node, ParamSet(_paramappend), "paramappend");
    ConfigUtils::writeConditions(&node, "queuewhen", _queuewhen);
    ConfigUtils::writeConditions(&node, "cancelwhen", _cancelwhen);
    return node;
  }
};

static inline QList<PfNode> grandchildren_list(
    const PfNode &node, const Utf8String &child_name) {
  QList<PfNode> list;
  for (auto child: node/child_name)
    for (auto grandchild: child.children())
      list << grandchild;
  return list;
}

PlanTaskAction::PlanTaskAction(Scheduler *scheduler, const PfNode &node)
    : Action(new PlanTaskActionData(
          scheduler, node.content_as_text(),
          ParamSet(node, "param"), node.has_child("force"),
          node.has_child("lone"),
          ParamSet(node, "paramappend").toUtf8Hash(),
          DisjunctionCondition(grandchildren_list(node, "queuewhen")),
          DisjunctionCondition(grandchildren_list(node, "cancelwhen"))
          )) {
}

PlanTaskAction::PlanTaskAction(Scheduler *scheduler, QByteArray taskId)
    : Action(new PlanTaskActionData(
          scheduler, taskId, ParamSet(), false, false,
          QHash<Utf8String,Utf8String>(), DisjunctionCondition(),
          DisjunctionCondition())) {
}

PlanTaskAction::PlanTaskAction(const PlanTaskAction &rhs) : Action(rhs) {
}

PlanTaskAction::~PlanTaskAction() {
}

DisjunctionCondition PlanTaskAction::queuewhen() const {
  auto d = specializedData<PlanTaskActionData>();
  return d ? d->_queuewhen : DisjunctionCondition();
}

DisjunctionCondition PlanTaskAction::cancelwhen() const {
  auto d = specializedData<PlanTaskActionData>();
  return d ? d->_cancelwhen : DisjunctionCondition();
}
