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
#include "plantaskaction.h"
#include "action_p.h"
#include "config/configutils.h"
#include "util/paramsprovidermerger.h"
#include "condition/disjunctioncondition.h"

class PlanTaskActionData : public ActionData {
public:
  QString _id;
  ParamSet _overridingParams;
  bool _force;
  QHash<QString,QString> _paramappend;
  DisjunctionCondition _queuewhen, _cancelwhen;

  PlanTaskActionData(
      Scheduler *scheduler, QString id, ParamSet params, bool force,
      QHash<QString,QString> paramappend, DisjunctionCondition queuewhen,
      DisjunctionCondition cancelwhen)
      : ActionData(scheduler), _id(id), _overridingParams(params),
        _force(force), _paramappend(paramappend),
        _queuewhen(queuewhen),
        _cancelwhen(ConfigUtils::guessCancelwhenCondition(
            queuewhen, cancelwhen)) { }
  void trigger(EventSubscription subscription, ParamSet eventContext,
               TaskInstance parentInstance) const override {
    if (!_scheduler)
      return;
    TaskInstancePseudoParamsProvider ppp = parentInstance.pseudoParams();
    ParamsProviderMerger ppm = ParamsProviderMerger(eventContext)(&ppp)
        (parentInstance.params());
    QString id;
    if (parentInstance.isNull()) {
      id = _id;
    } else {
      id = ParamSet().evaluate(_id, &ppm);
      QString idIfLocalToGroup = parentInstance.task().taskGroup().id()
                                 +"."+_id;
      if (_scheduler->taskExists(idIfLocalToGroup))
        id = idIfLocalToGroup;
    }
    TaskInstance herder = parentInstance.herder();
    ParamSet overridingParams;
    foreach (QString key, _overridingParams.keys())
      overridingParams.setValue(key, _overridingParams.value(key, &ppm));
    TaskInstanceList instances = _scheduler->planTask(
        id, overridingParams, _force, herder, _queuewhen, _cancelwhen);
    if (instances.isEmpty()) {
      Log::error(parentInstance.task().id(), parentInstance.idAsLong())
          << "plantask action failed to plan execution of task "
          << id << " within event subscription context "
          << subscription.subscriberName() << "|" << subscription.eventName();
      return;
    }
    for (auto childInstance: instances) {
      Log::info(parentInstance.task().id(), parentInstance.idAsLong())
          << "plantask action planned execution of task "
          << childInstance.task().id() << "/" << childInstance.groupId()
          << " with queue condition " << _queuewhen.toString()
          << " and cancel condition " << _cancelwhen.toString();
      if (_paramappend.isEmpty())
        continue;
      ParamsProviderMergerRestorer ppmr(&ppm);
      auto ppp = childInstance.pseudoParams();
      ppm.prepend(childInstance.params());
      ppm.prepend(&ppp);
      for (auto key: _paramappend.keys()) {
        auto value = ParamSet().evaluate(_paramappend.value(key), &ppm);
        parentInstance.paramAppend(key, ParamSet::escape(value));
      }
    }
  }
  QString toString() const override {
    return ">" + _id;
  }
  QString actionType() const override {
    return QStringLiteral("plantask");
  }
  bool mayCreateTaskInstances() const override {
    return true;
  }
  QString targetName() const override {
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
    ConfigUtils::writeParamSet(&node, ParamSet(_paramappend), "paramappend");
    ConfigUtils::writeConditions(&node, "queuewhen", _queuewhen);
    ConfigUtils::writeConditions(&node, "cancelwhen", _cancelwhen);
    return node;
  }
};

PlanTaskAction::PlanTaskAction(Scheduler *scheduler, PfNode node)
    : Action(new PlanTaskActionData(
          scheduler, node.contentAsString(),
          ConfigUtils::loadParamSet(node, "param"), node.hasChild("force"),
          ConfigUtils::loadParamSet(node, "paramappend").toHash(),
          DisjunctionCondition(node.grandChildrenByChildrenName("queuewhen")),
          DisjunctionCondition(node.grandChildrenByChildrenName("cancelwhen"))
          )) {
}

PlanTaskAction::PlanTaskAction(Scheduler *scheduler, QString taskId)
    : Action(new PlanTaskActionData(
          scheduler, taskId, ParamSet(), false,
          QHash<QString,QString>(), DisjunctionCondition(),
          DisjunctionCondition())) {
}

PlanTaskAction::PlanTaskAction(const PlanTaskAction &rhs) : Action(rhs) {
}

PlanTaskAction::~PlanTaskAction() {
}
