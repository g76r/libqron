/* Copyright 2014-2023 Hallowyn and others.
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
#ifndef SCHEDULERCONFIG_H
#define SCHEDULERCONFIG_H

#include "tasktemplate.h"
#include "cluster.h"
#include "calendar.h"
#include "alerterconfig.h"
#include "accesscontrolconfig.h"
#include "logfile.h"
#include "tasksroot.h"

class SchedulerConfigData;

/** Whole scheduler configuration */
class LIBQRONSHARED_EXPORT SchedulerConfig : public SharedUiItem {
public:
  SchedulerConfig();
  SchedulerConfig(const SchedulerConfig &other);
  /**
   * @param scheduler if 0, many scheduler-dependant objects (such as Action and
   * it subclasses) won't be able to behave fully correctly but the whole
   * parsing/writing/reading processes will work, which is convenient if the
   * config is not used within a scheduler but rather within a user interface
   * such as a configuration editor
   * @param applyLogConfig if true, call applyLogConfig() during configuration
   * parsing, as soon as possible, which may help having more detailed warning
   * and error messages about the configuration itself
   */
  SchedulerConfig(PfNode root, Scheduler *scheduler, bool applyLogConfig);
  ~SchedulerConfig();
  /** Should only be used by SharedUiItemsModels to get size and headers from
   * a non-null item. */
  static SchedulerConfig templateSchedulerConfig();
  SchedulerConfig &operator=(const SchedulerConfig &other) {
    SharedUiItem::operator=(other); return *this; }
  bool isNull() const;
  ParamSet params() const { return tasksRoot().params(); }
  ParamSet vars() const { return tasksRoot().vars(); }
  ParamSet instanceparams() const { return tasksRoot().instanceparams(); }
  TasksRoot tasksRoot() const;
  QMap<Utf8String,TaskGroup> taskgroups() const;
  QMap<Utf8String,TaskTemplate> tasktemplates() const;
  QMap<Utf8String,Task> tasks() const;
  QMap<Utf8String,Cluster> clusters() const;
  QMap<Utf8String,Host> hosts() const;
  QMap<Utf8String, Calendar> namedCalendars() const;
  QMap<Utf8String, PfNode> externalParams() const;
  QList<EventSubscription> onstart() const;
  QList<EventSubscription> onsuccess() const;
  QList<EventSubscription> onfailure() const;
  QList<EventSubscription> onlog() const;
  QList<EventSubscription> onnotice() const;
  QList<EventSubscription> onschedulerstart() const;
  QList<EventSubscription> onconfigload() const;
  QList<EventSubscription> allEventsSubscriptions() const;
  qint32 maxtotaltaskinstances() const;
  qint32 maxqueuedrequests() const;
  AlerterConfig alerterConfig() const;
  AccessControlConfig accessControlConfig() const;
  QList<LogFile> logfiles() const;
  /** Write current content as a PF tree, usefull in a config editor, does not
   * provide the original parsed file */
  PfNode toPfNode() const;
  /** Originaly parsed PF tree, if SchedulerConfig was constructed from a PF
   * tree, usefull for a configuration repository in a running scheduler. */
  PfNode originalPfNode() const;
  void copyLiveAttributesFromOldTasks(const QMap<Utf8String, Task> &oldTasks);
  void changeItem(SharedUiItem newItem, SharedUiItem oldItem,
                  QByteArray qualifier);
  void changeParams(ParamSet newParams, ParamSet oldParams, QByteArray setId);
  void applyLogConfig() const;

private:
  SchedulerConfigData *data();
  const SchedulerConfigData *data() const {
    return specializedData<SchedulerConfigData>(); }
  QByteArray recomputeId() const;
};

Q_DECLARE_METATYPE(SchedulerConfig)
Q_DECLARE_TYPEINFO(SchedulerConfig, Q_MOVABLE_TYPE);

#endif // SCHEDULERCONFIG_H
