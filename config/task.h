/* Copyright 2012-2022 Hallowyn and others.
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
#ifndef TASK_H
#define TASK_H

#include "libqron_global.h"
#include "util/paramset.h"
#include "taskgroup.h"
#include "modelview/shareduiitemlist.h"

class TaskData;
class TaskOrTemplateData;
class TaskPseudoParamsProvider;
class QDebug;
class PfNode;
class Trigger;
class CronTrigger;
class NoticeTrigger;
class Scheduler;
class EventSubscription;
class Calendar;
class TaskTemplate;
class RequestFormField;

/** Core task definition object. */
class LIBQRONSHARED_EXPORT Task : public SharedUiItem {
public:
  enum Mean {
    UnknownMean = 0, DoNothing, Local, Background, Ssh, Docker, Http, Scatter,
  };
  enum HerdingPolicy {
    NoFailure = 1,
    AllSuccess, // default
    OneSuccess, OwnStatus, NoWait,
    HerdingPolicyUnknown = 0
  };

  Task();
  Task(const Task &other);
  Task(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
       QHash<QString, Calendar> namedCalendars,
       QHash<QString,TaskTemplate> taskTemplates);
  /** Should only be used by SharedUiItemsModels to get size and headers from
   * a non-null item. */
  static Task dummyTask();
  Task &operator=(const Task &other) {
    SharedUiItem::operator=(other); return *this; }
  ParamSet params() const;
  void setParentParams(ParamSet parentParams);
  /** local id within group */
  QString localId() const;
  QString label() const;
  Task::Mean mean() const;
  static Task::Mean meanFromString(QString mean);
  static QString meanAsString(Task::Mean mean);
  QString meanAsString() const { return meanAsString(mean()); }
  /** List of valid mean strings, for ui or syntax check. */
  static QStringList validMeanStrings();
  QString command() const;
  QString statuscommand() const;
  QString abortcommand() const;
  QString target() const;
  void setTarget(QString target);
  QString info() const;
  TaskGroup taskGroup() const;
  void setTaskGroup(TaskGroup taskGroup);
  /** Resources consumed. */
  QHash<QString, qint64> resources() const;
  QDateTime lastExecution() const;
  void setLastExecution(QDateTime timestamp) const;
  QDateTime nextScheduledExecution() const;
  void setNextScheduledExecution(QDateTime timestamp) const;
  /** Maximum allowed simultaneous instances (includes running and queued
    * instances). Default: 1. */
  int maxInstances() const;
  /** Current running intances count (includes running and queued instances). */
  int runningCount() const;
  /** Atomic fetch-and-add of the current running instances count. */
  int fetchAndAddRunningCount(int valueToAdd) const;
  /** Executions count. */
  int executionsCount() const;
  /** Atomic fetch-and-add of the executions count. */
  int fetchAndAddExecutionsCount(int valueToAdd) const;
  QList<EventSubscription> onplan() const;
  QList<EventSubscription> onstart() const;
  QList<EventSubscription> onsuccess() const;
  QList<EventSubscription> onfailure() const;
  QList<EventSubscription> onstderr() const;
  QList<EventSubscription> onstdout() const;
  /** Events hash with "onsuccess", "onfailure"... key, mainly for UI purpose.
   * Not including group events subscriptions. */
  QList<EventSubscription> allEventsSubscriptions() const;
  bool mergeStderrIntoStdout() const;
  bool enabled() const;
  void setEnabled(bool enabled) const;
  bool lastSuccessful() const;
  void setLastSuccessful(bool successful) const;
  int lastReturnCode() const;
  void setLastReturnCode(int code) const;
  int lastDurationMillis() const;
  void setLastDurationMillis(int lastDurationMillis) const;
  quint64 lastTaskInstanceId() const;
  void setLastTaskInstanceId(quint64 lastTaskInstanceId) const;
  /** in millis, LLONG_MAX if not set */
  long long maxExpectedDuration() const;
  /** in millis, 0 if not set */
  long long minExpectedDuration() const;
  /** in millis, LLONG_MAX if not set */
  long long maxDurationBeforeAbort() const;
  ParamSet vars() const;
  ParamSet instanceparams() const;
  QString maxQueuedInstances() const;
  QString deduplicateCriterion() const;
  HerdingPolicy herdingPolicy() const;
  inline QString herdingPolicyAsString() const {
    return herdingPolicyAsString(herdingPolicy()); }
  static QString herdingPolicyAsString(HerdingPolicy v);
  static HerdingPolicy herdingPolicyFromString(QString v);
  QList<RequestFormField> requestFormFields() const;
  QString requestFormFieldsAsHtmlDescription() const;
  /** Create a ParamsProvider wrapper object to give access to ! pseudo params,
   * not to task params. */
  inline TaskPseudoParamsProvider pseudoParams() const;
  /** Cron triggers */
  QList<CronTrigger> cronTriggers() const;
  /** Notice triggers */
  QList<NoticeTrigger> noticeTriggers() const;
  /** Human readable list of other triggers, i.e. indirect triggers such
   * as the one implied by (onsuccess(requesttask foo)) on task bar.
   * Note that not all indirect triggers can be listed here since some cannot
   * be predicted, e.g. (onsuccess(requesttask %{baz})). Only predictable ones
   * are listed. */
  QStringList otherTriggers() const;
  void appendOtherTriggers(QString text);
  void clearOtherTriggers();
  SharedUiItemList<TaskTemplate> appliedTemplates() const;
  PfNode originalPfNode() const;
  PfNode toPfNode() const;
  /** to be called when activating a new configuration, to keep live attributes
   * such as lastReturnCode() or enabled() */
  void copyLiveAttributesFromOldTask(Task oldTask);
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);

private:
  TaskData *data();
  const TaskData *data() const { return specializedData<TaskData>(); }
};

/** ParamsProvider wrapper for pseudo params. */
class LIBQRONSHARED_EXPORT TaskPseudoParamsProvider : public ParamsProvider {
  Task _task;

public:
  inline TaskPseudoParamsProvider(Task task) : _task(task) { }
  QVariant paramValue(QString key, const ParamsProvider *context = 0,
                      QVariant defaultValue = QVariant(),
                      QSet<QString> alreadyEvaluated = QSet<QString>()
                      ) const override;
  QSet<QString> keys() const override;
};

inline TaskPseudoParamsProvider Task::pseudoParams() const {
  return TaskPseudoParamsProvider(*this);
}

Q_DECLARE_TYPEINFO(Task, Q_MOVABLE_TYPE);

#endif // TASK_H
