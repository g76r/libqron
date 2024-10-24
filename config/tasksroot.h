/* Copyright 2022-2024 Gregoire Barbier and others.
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
#ifndef TASKSROOT_H
#define TASKSROOT_H

#include "libqron_global.h"
#include "modelview/shareduiitem.h"

class TasksRootData;
class PfNode;
class Scheduler;
class EventSubscription;

class LIBQRONSHARED_EXPORT TasksRoot : public SharedUiItem {
public:
  TasksRoot();
  TasksRoot(const TasksRoot &other);
  TasksRoot(PfNode node, Scheduler *scheduler);
  TasksRoot &operator=(const TasksRoot &other) {
    SharedUiItem::operator=(other); return *this; }
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  ParamSet params() const;
  ParamSet vars() const;
  ParamSet instanceparams() const;
  QList<EventSubscription> onplan() const;
  QList<EventSubscription> onstart() const;
  QList<EventSubscription> onsuccess() const;
  QList<EventSubscription> onfailure() const;
  QList<EventSubscription> onstderr() const;
  QList<EventSubscription> onstdout() const;
  QList<EventSubscription> onnostderr() const;
  QList<EventSubscription> allEventSubscriptions() const;
  bool mergeStdoutIntoStderr() const;
  QList<PfNode> originalPfNodes() const;
  PfNode toPfNode() const;

private:
  inline TasksRootData *data();
  inline const TasksRootData *data() const;
};

Q_DECLARE_TYPEINFO(TasksRoot, Q_MOVABLE_TYPE);

#endif // TASKSROOT_H
