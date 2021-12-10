/* Copyright 2012-2021 Hallowyn and others.
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
#ifndef TASKINSTANCE_H
#define TASKINSTANCE_H

#include <QSharedDataPointer>
#include "config/task.h"
#include "util/paramset.h"
#include <QDateTime>
#include "config/host.h"
#include "util/paramset.h"
#include "modelview/shareduiitem.h"
#include "modelview/shareduiitemlist.h"

class TaskInstanceData;
class TaskInstancePseudoParamsProvider;

/** Instance of a task created when the execution is requested and used to track
 * the execution until it is finished and even after. */
class LIBQRONSHARED_EXPORT TaskInstance : public SharedUiItem {
public:
  enum TaskInstanceStatus { Queued, Running, Success, Failure, Canceled };
  TaskInstance();
  TaskInstance(const TaskInstance &);
  TaskInstance(Task task, bool force, TaskInstance workflowTaskInstance,
               ParamSet overridingParams);
  TaskInstance(Task task, quint64 groupId, bool force,
               TaskInstance workflowTaskInstance,
               ParamSet overridingParams);
  TaskInstance &operator=(const TaskInstance &other) {
    SharedUiItem::operator=(other); return *this; }
  Task task() const;
  ParamSet params() const;
  void overrideParam(QString key, QString value);
  ParamSet overridingParams() const;
  quint64 idAsLong() const;
  quint64 groupId() const;
  QDateTime requestDatetime() const;
  QDateTime startDatetime() const;
  void setStartDatetime(QDateTime datetime
                        = QDateTime::currentDateTime()) const;
  void setEndDatetime(QDateTime datetime = QDateTime::currentDateTime()) const;
  QDateTime endDatetime() const;
  qint64 queuedMillis() const;
  qint64 runningMillis() const;
  qint64 totalMillis() const;
  qint64 liveTotalMillis() const;
  bool success() const;
  void setSuccess(bool success) const;
  int returnCode() const;
  void setReturnCode(int returnCode) const;
  /** Note that this is the exact target on which the task is running/has been
    * running, i.e. if the task target was a cluster, this is the host which
    * was choosen within the cluster.
    * Most of the time, return a null Host when the task instance is still
    * queued. */
  Host target() const;
  void setTarget(Host target) const;
  /** Create a ParamsProvider wrapper object to give access to ! pseudo params,
   * not to task params. */
  inline TaskInstancePseudoParamsProvider pseudoParams() const;
  void setTask(Task task);
  bool force() const;
  TaskInstanceStatus status() const;
  static QString statusAsString(TaskInstance::TaskInstanceStatus status);
  QString statusAsString() const {
    return statusAsString(status()); }
  /** @return true iff status != Queued or Running */
  bool finished() const {
    switch(status()) {
    case Queued:
    case Running:
      return false;
    default:
      return true;
    }
  }
  QString command() const;
  bool abortable() const;
  void setAbortable(bool abortable = true) const;
  /** Return workflow task instance when called on a subtask instance,
   * or *this if called on a workflow instance, otherwise a null instance. */
  TaskInstance workflowTaskInstance() const;

private:
  TaskInstanceData *data();
  const TaskInstanceData *data() const {
    return specializedData<TaskInstanceData>(); }
};

Q_DECLARE_METATYPE(TaskInstance)
Q_DECLARE_TYPEINFO(TaskInstance, Q_MOVABLE_TYPE);

/** ParamsProvider wrapper for pseudo params. */
class LIBQRONSHARED_EXPORT TaskInstancePseudoParamsProvider
    : public ParamsProvider {
  TaskInstance _taskInstance;
  TaskPseudoParamsProvider _taskPseudoParams;

public:
  inline TaskInstancePseudoParamsProvider(TaskInstance taskInstance)
    : _taskInstance(taskInstance),
      _taskPseudoParams(taskInstance.task().pseudoParams()) { }
  QVariant paramValue(QString key, const ParamsProvider *context = 0,
                      QVariant defaultValue = QVariant(),
                      QSet<QString> alreadyEvaluated = QSet<QString>()) const;
};

inline TaskInstancePseudoParamsProvider TaskInstance::pseudoParams() const {
  return TaskInstancePseudoParamsProvider(*this);
}

class LIBQRONSHARED_EXPORT TaskInstanceList
    : public SharedUiItemList<TaskInstance> {
public:
  TaskInstanceList() { }
  TaskInstanceList(const TaskInstanceList &other)
    : SharedUiItemList<TaskInstance>(other) { }
  TaskInstanceList(const SharedUiItemList<TaskInstance> &other)
    : SharedUiItemList<TaskInstance>(other) { }
  TaskInstanceList(const QList<TaskInstance> &other)
    : SharedUiItemList<TaskInstance>(other) { }
  operator QList<quint64>() const {
    QList<quint64> list;
    for (const TaskInstance &taskInstance : *this)
      list.append(taskInstance.idAsLong());
    return list;
  }
  operator QStringList() const {
    QStringList list;
    for (const TaskInstance &taskInstance : *this)
      list.append(taskInstance.id());
    return list;
  }
};

Q_DECLARE_METATYPE(TaskInstanceList)
Q_DECLARE_TYPEINFO(TaskInstanceList, Q_MOVABLE_TYPE);

#endif // TASKINSTANCE_H
