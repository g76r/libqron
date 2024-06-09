/* Copyright 2012-2024 Hallowyn and others.
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

#include "taskgroup.h"
#include "modelview/shareduiitemlist.h"

class TaskData;
class TaskOrTemplateData;
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
       QMap<Utf8String, Calendar> namedCalendars,
       QMap<Utf8String, TaskTemplate> taskTemplates);
  /** Should only be used by SharedUiItemsModels to get size and headers from
   * a non-null item. */
  static Task dummyTask();
  Task &operator=(const Task &other) {
    SharedUiItem::operator=(other); return *this; }
  ParamSet params() const;
  void setParentParams(ParamSet parentParams);
  /** local id within group */
  QByteArray localId() const;
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
  QMap<Utf8String,qint64> resources() const;
  QDateTime lastExecution() const;
  void setLastExecution(QDateTime timestamp) const;
  QDateTime nextScheduledExecution() const;
  void setNextScheduledExecution(QDateTime timestamp) const;
  /** Maximum allowed simultaneous instances (includes running and queued
    * instances). Default: 1. */
  int maxInstances() const;
  int maxPerHost() const;
  /** Current running intances count (includes running and queued instances). */
  int runningCount() const;
  /** Max time to try to execute task if it fails. */
  int maxTries() const;
  /** Time to wait before retrying, in ms. */
  int millisBetweenTries() const;
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
  bool mergeStdoutIntoStderr() const;
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
  QString deduplicateStrategy() const;
  HerdingPolicy herdingPolicy() const;
  inline QString herdingPolicyAsString() const {
    return herdingPolicyAsString(herdingPolicy()); }
  static QString herdingPolicyAsString(HerdingPolicy v);
  static HerdingPolicy herdingPolicyFromString(QString v);
  QMap<QString, RequestFormField> requestFormFields() const;
  QString requestFormFieldsAsHtmlDescription() const;
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
  SharedUiItemList appliedTemplates() const;
  QList<PfNode> originalPfNodes() const;
  PfNode toPfNode() const;
  /** to be called when activating a new configuration, to keep live attributes
   * such as lastReturnCode() or enabled() */
  void copyLiveAttributesFromOldTask(const Task &oldTask);
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  void detach();

private:
  inline TaskData *data();
  inline const TaskData *data() const;
};

Q_DECLARE_TYPEINFO(Task, Q_MOVABLE_TYPE);

#endif // TASK_H
