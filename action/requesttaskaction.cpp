/* Copyright 2013-2021 Hallowyn and others.
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
#include "requesttaskaction.h"
#include "action_p.h"
#include "config/configutils.h"
#include "util/paramsprovidermerger.h"

class RequestTaskActionData : public ActionData {
public:
  QString _id;
  ParamSet _overridingParams;
  bool _force, _lone;
  RequestTaskActionData(
      Scheduler *scheduler, QString id, ParamSet params, bool force, bool lone)
    : ActionData(scheduler), _id(id), _overridingParams(params),
      _force(force), _lone(lone) { }
  inline ParamSet evaluatedOverrindingParams(ParamSet eventContext,
                                             TaskInstance instance) const {
    ParamSet overridingParams;
    TaskInstancePseudoParamsProvider ppp = instance.pseudoParams();
    ParamsProviderMerger ppm = ParamsProviderMerger(eventContext)(&ppp)
        (instance.params());
    foreach (QString key, _overridingParams.keys())
      overridingParams.setValue(key, _overridingParams.value(key, &ppm));
    //Log::fatal() << "******************* " << eventContext << overridingParams
    //             << _overridingParams;
    return overridingParams;
  }
  void trigger(EventSubscription subscription, ParamSet eventContext,
               TaskInstance parentInstance) const override {
    if (_scheduler) {
      QString id;
      if (parentInstance.isNull()) {
        id = _id;
      } else {
        TaskInstancePseudoParamsProvider ppp = parentInstance.pseudoParams();
        id = eventContext.evaluate(_id, &ppp);
        QString idIfLocalToGroup = parentInstance.task().taskGroup().id()
            +"."+_id;
        if (_scheduler->taskExists(idIfLocalToGroup))
          id = idIfLocalToGroup;
      }
      TaskInstance herder = _lone ? TaskInstance() : parentInstance.herder();
      TaskInstanceList instances = _scheduler->syncRequestTask(
          id, evaluatedOverrindingParams(eventContext, parentInstance), _force,
          herder);
      if (instances.isEmpty())
        Log::error(parentInstance.task().id(), parentInstance.idAsLong())
            << "requesttask action failed to request execution of task "
            << id << " within event subscription context "
            << subscription.subscriberName() << "|" << subscription.eventName();
      else
        foreach (TaskInstance childInstance, instances)
          Log::info(parentInstance.task().id(), parentInstance.idAsLong())
              << "requesttask action requested execution of task "
              << childInstance.task().id() << "/" << childInstance.groupId();
    }

  }
  QString toString() const override {
    return "*" + _id;
  }
  QString actionType() const override {
    return QStringLiteral("requesttask");
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
    if (_lone)
      node.appendChild(PfNode("lone"));
    return node;
  }
};

RequestTaskAction::RequestTaskAction(Scheduler *scheduler, PfNode node)
  : Action(new RequestTaskActionData(
             scheduler, node.contentAsString(),
             ConfigUtils::loadParamSet(node, "param"), node.hasChild("force"),
             node.hasChild("lone"))) {
}

RequestTaskAction::RequestTaskAction(Scheduler *scheduler, QString taskId)
  : Action(new RequestTaskActionData(
             scheduler, taskId, ParamSet(), false, false)) {
}

RequestTaskAction::RequestTaskAction(const RequestTaskAction &rhs)
  : Action(rhs) {
}

RequestTaskAction::~RequestTaskAction() {
}
