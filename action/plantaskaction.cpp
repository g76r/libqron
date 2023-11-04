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
    auto parentTask = parentInstance.task();
    if (!parentInstance.isNull()) {
      QString idIfLocalToGroup = parentTask.taskGroup().id()+"."+id;
      if (_scheduler->taskExists(idIfLocalToGroup.toUtf8()))
        id = idIfLocalToGroup;
    }
    quint64 herdid = _lone ? 0 : parentInstance.herdid();
    ParamSet overridingParams;
    for (auto key: _overridingParams.paramKeys())
      overridingParams.insert(
            key, PercentEvaluator::escape(
              PercentEvaluator::eval(
                _overridingParams.paramRawUtf8(key), context)));
    if (!parentInstance.isNull()) {
      overridingParams.insert("!parenttaskinstanceid", parentInstance.id());
      overridingParams.insert("!parenttaskid", parentInstance.taskId());
      overridingParams.insert("!parenttasklocalid",
                              parentTask.localId());
    }
    TaskInstanceList instances = _scheduler->planTask(
      id, overridingParams, _force, herdid, _queuewhen, _cancelwhen);
    if (instances.isEmpty()) {
      Log::error(parentInstance.taskId(), parentInstance.idAsLong())
          << "plantask action failed to plan execution of task "
          << id << " within event subscription context "
          << subscription.subscriberName() << "|" << subscription.eventName();
      return;
    }
    for (auto sui: instances) {
      auto childInstance = sui.casted<TaskInstance>();
      Log::info(parentInstance.taskId(), parentInstance.idAsLong())
          << "plantask action planned execution of task "
          << childInstance.taskId() << "/" << childInstance.groupId()
          << " with queue condition " << _queuewhen.toString()
          << " and cancel condition " << _cancelwhen.toString();
      if (_paramappend.isEmpty())
        continue;
      context->prepend(&childInstance);
      if (herdid != 0) {
        for (auto key: _paramappend.keys()) {
          auto value =
              PercentEvaluator::eval_utf8(_paramappend.value(key), context);
          _scheduler->taskInstanceParamAppend(
                herdid, key, PercentEvaluator::escape(value));
        }
      }
      context->pop_front();
    }
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
      node.appendChild(PfNode("force"));
    if (_lone)
      node.appendChild(PfNode("lone"));
    ConfigUtils::writeParamSet(&node, ParamSet(_paramappend), "paramappend");
    ConfigUtils::writeConditions(&node, "queuewhen", _queuewhen);
    ConfigUtils::writeConditions(&node, "cancelwhen", _cancelwhen);
    return node;
  }
};

PlanTaskAction::PlanTaskAction(Scheduler *scheduler, PfNode node)
    : Action(new PlanTaskActionData(
          scheduler, node.contentAsUtf16(),
          ParamSet(node, "param"), node.hasChild("force"),
          node.hasChild("lone"),
          ParamSet(node, "paramappend").toUtf8Hash(),
          DisjunctionCondition(node.grandChildrenByChildrenName("queuewhen")),
          DisjunctionCondition(node.grandChildrenByChildrenName("cancelwhen"))
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
