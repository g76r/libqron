/* Copyright 2012-2023 Hallowyn and others.
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
#ifndef TASKGROUP_H
#define TASKGROUP_H

#include "libqron_global.h"
#include "util/paramset.h"
#include "modelview/shareduiitem.h"

class TaskGroupData;
class Task;
class PfNode;
class QDebug;
class Scheduler;
class EventSubscription;
class TaskInstance;


/** A task group is a mean to tie tasks together to make them share some
 * configuration and to indicate that they are related (e.g. they belong to
 * the same application or have the same criticity). */
class LIBQRONSHARED_EXPORT TaskGroup : public SharedUiItem {
public:
  TaskGroup();
  TaskGroup(const TaskGroup &other);
  TaskGroup(PfNode node, SharedUiItem parent, Scheduler *scheduler);
  /** pseudo constructor for models */
  TaskGroup(QByteArray id);
  TaskGroup &operator=(const TaskGroup &other) {
    SharedUiItem::operator=(other); return *this; }
  /** return "foo.bar" for group "foo.bar.baz" and {} for group "foo". */
  QByteArray parentGroupId() const { return parentGroupId(id()); }
  /** return "foo.bar" for group "foo.bar.baz" and {} for group "foo". */
  static QByteArray parentGroupId(QByteArray groupId);
  QString label() const;
  ParamSet params() const;
  QList<EventSubscription> onplan() const;
  QList<EventSubscription> onstart() const;
  QList<EventSubscription> onsuccess() const;
  QList<EventSubscription> onfailure() const;
  QList<EventSubscription> onstderr() const;
  QList<EventSubscription> onstdout() const;
  ParamSet vars() const;
  ParamSet instanceparams() const;
  QList<EventSubscription> allEventSubscriptions() const;
  PfNode originalPfNode() const;
  PfNode toPfNode() const;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);

private:
  TaskGroupData *data();
  const TaskGroupData *data() const { return specializedData<TaskGroupData>(); }
};

Q_DECLARE_TYPEINFO(TaskGroup, Q_MOVABLE_TYPE);

#endif // TASKGROUP_H
