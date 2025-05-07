/* Copyright 2013-2025 Hallowyn and others.
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
#include "action_p.h"
#include "postnoticeaction.h"
#include "raisealertaction.h"
#include "cancelalertaction.h"
#include "emitalertaction.h"
#include "plantaskaction.h"
#include "logaction.h"
#include "config/configutils.h"
#include "requesturlaction.h"
#include "writefileaction.h"
#include "donothingaction.h"
#include "paramappendaction.h"
#include "overrideparamaction.h"
#include "execaction.h"

Action::Action() {
}

Action::Action(const Action &rhs) : d(rhs.d) {
}

Action::Action(ActionData *data) : d(data) {
}

Action &Action::operator=(const Action &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

Action::~Action() {
}

ActionData::~ActionData() {
}

void Action::trigger(
    EventSubscription subscription, ParamsProviderMerger *context,
    TaskInstance instance) const {
  if (d)
    d->trigger(subscription, context, instance);
}

Utf8String ActionData::toString() const {
  return "Action";
}

Utf8String ActionData::actionType() const {
  return "unknown"_u8;
}

bool ActionData::mayCreateTaskInstances() const {
  return false;
}

void ActionData::trigger(
    EventSubscription subscription, ParamsProviderMerger *context,
    TaskInstance instance) const {
  Q_UNUSED(context)
  Q_UNUSED(instance)
  Log::error() << "ActionData::trigger() called whereas it should never, "
                  "from subscription " << subscription.subscriberName()
                  << "|" << subscription.eventName();
}

Utf8String Action::toString() const {
  return d ? d->toString() : Utf8String();
}

Utf8String Action::actionType() const {
  return d ? d->actionType() : Utf8String();
}

bool Action::mayCreateTaskInstances() const {
  return d ? d->mayCreateTaskInstances() : false;
}

Utf8StringList Action::toStringList(QList<Action> list) {
  Utf8StringList sl;
  for (const auto &a: list)
    sl.append(a.toString());
  return sl;
}

Utf8String Action::targetName() const {
  return d ? d->targetName() : Utf8String();
}

Utf8String ActionData::targetName() const {
  return {};
}

ParamSet Action::params() const {
  return d ? d->params() : ParamSet();
}

ParamSet ActionData::params() const {
  return ParamSet();
}

PfNode Action::toPfNode() const {
  PfNode node;
  if (d)
    node = d->toPfNode();
  return node;
}

PfNode ActionData::toPfNode() const {
  return PfNode(); // should never happen
}

static RadixTree<std::function<Action(const PfNode&,Scheduler*)>> _actionBuilders {
{ "postnotice", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
  return PostNoticeAction(scheduler, node); } },
{ "raisealert", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return RaiseAlertAction(scheduler, node); } },
{ "cancelalert", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return CancelAlertAction(scheduler, node); } },
{ "emitalert", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return EmitAlertAction(scheduler, node); } },
{ "requesttask", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   Log::warning() << "requesttask action is deprecated, use plantask instead";
   return PlanTaskAction(scheduler, node); } },
{ "plantask", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return PlanTaskAction(scheduler, node); } },
{ "requesturl", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return RequestUrlAction(scheduler, node); } },
{ "writefile", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return WriteFileAction(scheduler, node); } },
{ "log", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return LogAction(scheduler, node); } },
{ { "donothing", "stop" }, [](const PfNode &node, Scheduler *) STATIC_LAMBDA -> Action {
   return DoNothingAction(node.name()); } },
{ "paramappend", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return ParamAppendAction(scheduler, node); } },
{ "overrideparam", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return OverrideParamAction(scheduler, node); } },
{ "exec", [](const PfNode &node, Scheduler *scheduler) STATIC_LAMBDA -> Action {
   return ExecAction(scheduler, node); } },
};

Action Action::createAction(const PfNode &node, Scheduler *scheduler) {
  Action action;
  auto builder = _actionBuilders.value(node.name());
  if (builder)
    action = builder(node, scheduler);
  if (action.isNull()) {
    Log::error() << "unknown action type: " << node.name() << " at "
                 << node.position();
  }
  return action;
}
