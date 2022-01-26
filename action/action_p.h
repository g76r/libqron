/* Copyright 2013-2022 Hallowyn and others.
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
#ifndef ACTION_P_H
#define ACTION_P_H

#include "action.h"
#include "sched/scheduler.h"
#include "config/eventsubscription.h"

class LIBQRONSHARED_EXPORT ActionData : public QSharedData {
public:
  Scheduler *_scheduler;
  QStringList _commentsList;
  explicit ActionData(Scheduler *scheduler = 0) : _scheduler(scheduler) { }
  virtual ~ActionData();
  /** Human readable description of action */
  virtual QString toString() const;
  /** Type of action for programmatic test, e.g. "postnotice" */
  virtual QString actionType() const;
  /** Action can create task instances, e.g. "plantask" */
  virtual bool mayCreateTaskInstances() const;
  /** Default: do nothing */
  virtual void trigger(
      EventSubscription subscription, ParamsProviderMerger *context,
      TaskInstance instance) const;
  virtual QString targetName() const;
  virtual PfNode toPfNode() const;
  virtual ParamSet params() const;
};

#endif // ACTION_P_H
