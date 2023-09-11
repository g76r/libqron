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
#ifndef TASKINSTANCE_H
#define TASKINSTANCE_H

#include "config/task.h"
#include "config/host.h"
#include "condition/condition.h"
#include "modelview/shareduiitemlist.h"

class TaskInstanceData;
class TaskInstanceList;

/** Instance of a task created when the execution is requested and used to track
 * the execution until it is finished and even after. */
class LIBQRONSHARED_EXPORT TaskInstance : public SharedUiItem {
public:
  enum TaskInstanceStatus { Planned, Queued, Running, Waiting, Success, Failure,
                            Canceled };
  TaskInstance();
  TaskInstance(const TaskInstance &);
  TaskInstance(Task task, bool force, ParamSet params, quint64 herdid,
               Condition queuewhen = Condition(),
               Condition cancelwhen = Condition());
  TaskInstance(Task task, quint64 groupId, bool force,
               ParamSet params, quint64 herdid);
  TaskInstance &operator=(const TaskInstance &other) {
    SharedUiItem::operator=(other); return *this; }
  Task task() const;
  void setParam(QString key, QString value) const;
  /** Either set param if empty or append a space followed by value to current
   * value */
  void paramAppend(QString key, QString value) const;
  ParamSet params() const;
  quint64 idAsLong() const;
  /** @return string of the form "taskid/taskinstanceid" */
  QString idSlashId() const { return task().id()+"/"+id(); }
  quint64 groupId() const;
  QDateTime creationDatetime() const;
  QDateTime queueDatetime() const;
  void setQueueDatetime(QDateTime datetime= QDateTime::currentDateTime()) const;
  QDateTime startDatetime() const;
  void setStartDatetime(QDateTime datetime
                        = QDateTime::currentDateTime()) const;
  QDateTime stopDatetime() const;
  void setStopDatetime(QDateTime datetime = QDateTime::currentDateTime()) const;
  QDateTime finishDatetime() const;
  void setFinishDatetime(QDateTime datetime
                         = QDateTime::currentDateTime()) const;
  qint64 plannedMillis() const;
  qint64 queuedMillis() const;
  qint64 runningMillis() const;
  qint64 waitingMillis() const;
  qint64 durationMillis() const;
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
  void setTask(Task task);
  bool force() const;
  TaskInstanceStatus status() const;
  static QString statusAsString(TaskInstance::TaskInstanceStatus status);
  QString statusAsString() const {
    return statusAsString(status()); }
  /** @return true iff status != Queued or Running */
  bool isFinished() const {
    switch(status()) {
    case Planned:
    case Queued:
    case Running:
    case Waiting:
      return false;
    case Success:
    case Failure:
    case Canceled:
      return true;
    }
    return true;
  }
  QString command() const;
  bool abortable() const;
  void setAbortable(bool abortable = true) const;
  quint64 herdid() const;
  bool isHerder() const { return herdid() == idAsLong(); }
  void appendToHerdedTasksCaption(QString text) const;
  void consumeOneTry() const;
  void consumeAllTries() const;
  int remainingTries() const;
  int currentTry() const;
  Condition queuewhen() const;
  Condition cancelwhen() const;
  /** vars(), evaluated and protected to respect shell environment rules
   * (i.e. no special character and prefix with _ if first character is a digit)
   */
  QMap<QString,QString> varsAsEnv() const;
  /** vars() keys protected to respect shell environment rules
   * (i.e. no special character and prefix with _ if first character is a digit)
   * warning: since the keys are modified they can be used to lookup into
   * varsAsEnv() but no longer in task().vars() (at less not always)
   */
  //QStringList varsAsEnvKeys() const;
  /** vars(), evaluated and protected to respect internet headers rules
   * (rfc5322, internet message format, including http, i.e. no ':' in name,
   * no end-of-line in value)
   */
  QMap<QString,QString> varsAsHeaders() const;
  /** vars() keys protected to respect internet headers rules
   * (rfc5322, internet message format, including http, i.e. no ':')
   * warning: since the keys are modified they can be used to lookup into
   * varsAsHeaders() but no longer in task().vars() (at less not always)
   */
  //QStringList varsAsHeadersKeys() const;

private:
  inline TaskInstanceData *data();
  inline const TaskInstanceData *data() const;
};

Q_DECLARE_METATYPE(TaskInstance)
Q_DECLARE_TYPEINFO(TaskInstance, Q_MOVABLE_TYPE);

class LIBQRONSHARED_EXPORT TaskInstanceList
    : public SharedUiItemList {
public:
  TaskInstanceList() { }
  TaskInstanceList(const TaskInstanceList &other)
    : SharedUiItemList(other) { }
  TaskInstanceList(const SharedUiItemList &other)
    : SharedUiItemList(other) { }
  TaskInstanceList(const QList<TaskInstance> &other)
    : SharedUiItemList(other) { }
  operator QList<quint64>() const {
    QList<quint64> list;
    for (auto sui : *this)
      list.append(sui.casted<TaskInstance>().idAsLong());
    return list;
  }
  operator QStringList() const {
    QStringList list;
    for (auto sui : *this) {
      auto i = sui.casted<TaskInstance>();
      list.append(i.task().id()+"/"+i.id());
    }
    return list;
  }
};

Q_DECLARE_METATYPE(TaskInstanceList)
Q_DECLARE_TYPEINFO(TaskInstanceList, Q_MOVABLE_TYPE);

#endif // TASKINSTANCE_H
