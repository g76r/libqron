/* Copyright 2013-2023 Hallowyn and others.
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

QString ActionData::toString() const {
  return "Action";
}

QString ActionData::actionType() const {
  return "unknown";
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

QString Action::toString() const {
  return d ? d->toString() : QString();
}

QString Action::actionType() const {
  return d ? d->actionType() : QString();
}

bool Action::mayCreateTaskInstances() const {
  return d ? d->mayCreateTaskInstances() : false;
}

QStringList Action::toStringList(QList<Action> list) {
  QStringList sl;
  foreach (const Action e, list)
    sl.append(e.toString());
  return sl;
}

QString Action::targetName() const {
  return d ? d->targetName() : QString();
}

QString ActionData::targetName() const {
  return QString();
}

ParamSet Action::params() const {
  return d ? d->params() : ParamSet();
}

ParamSet ActionData::params() const {
  return ParamSet();
}

PfNode Action::toPfNode() const {
  PfNode node;
  if (d) {
    node = d->toPfNode();
    // MAYDO write comments before content
    ConfigUtils::writeComments(&node, d->_commentsList);
  }
  return node;
}

PfNode ActionData::toPfNode() const {
  return PfNode(); // should never happen
}

static RadixTree<std::function<Action(PfNode,Scheduler*)>> _actionBuilders {
{ "postnotice", [](PfNode node, Scheduler *scheduler) -> Action {
  return PostNoticeAction(scheduler, node); } },
{ "raisealert", [](PfNode node, Scheduler *scheduler) -> Action {
   return RaiseAlertAction(scheduler, node); } },
{ "cancelalert", [](PfNode node, Scheduler *scheduler) -> Action {
   return CancelAlertAction(scheduler, node); } },
{ "emitalert", [](PfNode node, Scheduler *scheduler) -> Action {
   return EmitAlertAction(scheduler, node); } },
{ "requesttask", [](PfNode node, Scheduler *scheduler) -> Action {
   Log::warning() << "requesttask action is deprecated, use plantask instead";
   return PlanTaskAction(scheduler, node); } },
{ "plantask", [](PfNode node, Scheduler *scheduler) -> Action {
   return PlanTaskAction(scheduler, node); } },
{ "requesturl", [](PfNode node, Scheduler *scheduler) -> Action {
   return RequestUrlAction(scheduler, node); } },
{ "writefile", [](PfNode node, Scheduler *scheduler) -> Action {
   return WriteFileAction(scheduler, node); } },
{ "log", [](PfNode node, Scheduler *scheduler) -> Action {
   return LogAction(scheduler, node); } },
{ { "donothing", "stop" }, [](PfNode node, Scheduler *) -> Action {
   return DoNothingAction(node.name()); } },
{ "paramappend", [](PfNode node, Scheduler *scheduler) -> Action {
   return ParamAppendAction(scheduler, node); } },
{ "overrideparam", [](PfNode node, Scheduler *scheduler) -> Action {
   return OverrideParamAction(scheduler, node); } },
{ "exec", [](PfNode node, Scheduler *scheduler) -> Action {
   return ExecAction(scheduler, node); } },
};

Action Action::createAction(PfNode node, Scheduler *scheduler) {
  Action action;
  auto builder = _actionBuilders.value(node.name());
  if (builder)
    action = builder(node, scheduler);
  if (action.isNull()) {
    Log::error() << "unknown action type: " << node.name();
  } else {
    ConfigUtils::loadComments(node, &action.d->_commentsList);
  }
  return action;
}
