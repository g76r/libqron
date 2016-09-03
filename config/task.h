/* Copyright 2012-2016 Hallowyn and others.
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
#include <QSharedData>
#include "util/paramset.h"
#include <QSet>
#include "taskgroup.h"
#include "requestformfield.h"
#include <QList>
#include "modelview/shareduiitem.h"

class TaskData;
class TaskPseudoParamsProvider;
class QDebug;
class PfNode;
class Trigger;
class CronTrigger;
class NoticeTrigger;
class Scheduler;
class EventSubscription;
class Step;
class StepInstance;
class Calendar;
class WorkflowTransitionData;

/** Data object for workflow transitions, i.e. links between a source step,
 * an event name and a destination step, within a given workflow task.
 *
 * Source step can be the start step ($start) or a workflow trigger
 * ($crontrigger_XXX or $noticetrigger_XXX).
 *
 * id() returns ${workflowid}:${sourcelocalid}:${eventname}:${targetlocalid}
 * localId() returns ${sourcelocalid}:${eventname}:${targetlocalid}
 */
class LIBQRONSHARED_EXPORT WorkflowTransition : public SharedUiItem {
public:
  WorkflowTransition();
  WorkflowTransition(const WorkflowTransition &other);
  WorkflowTransition(QString workflowId, QString sourceLocalId,
                     QString eventName, QString targetLocalId);
  ~WorkflowTransition();
  WorkflowTransition &operator=(const WorkflowTransition &other) {
    SharedUiItem::operator=(other); return *this; }
  void detach();
  QString workflowId() const;
  void setWorkflowId(QString workflowId);
  QString sourceLocalId() const;
  QString eventName() const;
  void setEventName(QString eventName);
  QString targetLocalId() const;
  /** Same as id() w/o leading workflowid,
   * i.e. ${sourcelocalid}:${eventname}:${targetlocalid} */
  QString localId() const;

private:
  WorkflowTransitionData *data();
  const WorkflowTransitionData *data() const {
    return (const WorkflowTransitionData*)SharedUiItem::data(); }
};

Q_DECLARE_METATYPE(WorkflowTransition)
Q_DECLARE_TYPEINFO(WorkflowTransition, Q_MOVABLE_TYPE);

/** Core task definition object, being it a standalone task or workflow. */
class LIBQRONSHARED_EXPORT Task : public SharedUiItem {
public:
  enum EnqueuePolicy { // was DiscardAliasesOnStart
    EnqueueAndDiscardQueued = 1, // default, was: DiscardAll
    EnqueueAll = 2, // was: DiscardNone
    EnqueueUntilMaxInstances = 4,
    EnqueuePolicyUnknown = 0
  };
  enum Mean {
    UnknownMean = 0, DoNothing, Local, Workflow, Ssh, Http
  };

  Task();
  Task(const Task &other);
  Task(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
       QString workflowTaskId, QHash<QString, Calendar> namedCalendars);
  /** Should only be used by SharedUiItemsModels to get size and headers from
   * a non-null item. */
  static Task templateTask();
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
  QString target() const;
  void setTarget(QString target);
  QString info() const;
  TaskGroup taskGroup() const;
  void setTaskGroup(TaskGroup taskGroup);
  /** Resources consumed. */
  QHash<QString, qint64> resources() const;
  QString resourcesAsString() const;
  QDateTime lastExecution() const;
  void setLastExecution(QDateTime timestamp) const;
  QDateTime nextScheduledExecution() const;
  void setNextScheduledExecution(QDateTime timestamp) const;
  /** Maximum allowed simultaneous instances (includes running and queued
    * instances). Default: 1. */
  int maxInstances() const;
  /** Current intances count (includes running and queued instances). */
  int instancesCount() const;
  /** Atomic fetch-and-add of the current instances count. */
  int fetchAndAddInstancesCount(int valueToAdd) const;
  /** Executions count. */
  int executionsCount() const;
  /** Atomic fetch-and-add of the executions count. */
  int fetchAndAddExecutionsCount(int valueToAdd) const;
  QList<QRegExp> stderrFilters() const;
  void appendStderrFilter(QRegExp filter);
  void triggerStartEvents(TaskInstance instance) const;
  void triggerSuccessEvents(TaskInstance instance) const;
  void triggerFailureEvents(TaskInstance instance) const;
  QList<EventSubscription> onstartEventSubscriptions() const;
  QList<EventSubscription> onsuccessEventSubscriptions() const;
  QList<EventSubscription> onfailureEventSubscriptions() const;
  /** Events hash with "onsuccess", "onfailure"... key, mainly for UI purpose.
   * Not including group events subscriptions. */
  QList<EventSubscription> allEventsSubscriptions() const;
  bool enabled() const;
  void setEnabled(bool enabled) const;
  bool lastSuccessful() const;
  void setLastSuccessful(bool successful) const;
  int lastReturnCode() const;
  void setLastReturnCode(int code) const;
  int lastTotalMillis() const;
  void setLastTotalMillis(int lastTotalMillis) const;
  quint64 lastTaskInstanceId() const;
  void setLastTaskInstanceId(quint64 lastTaskInstanceId) const;
  /** in millis, LLONG_MAX if not set */
  long long maxExpectedDuration() const;
  /** in millis, 0 if not set */
  long long minExpectedDuration() const;
  /** in millis, LLONG_MAX if not set */
  long long maxDurationBeforeAbort() const;
  ParamSet setenv() const;
  ParamSet unsetenv() const;
  EnqueuePolicy enqueuePolicy() const;
  inline QString enqueuePolicyAsString() const {
    return enqueuePolicyAsString(enqueuePolicy()); }
  static QString enqueuePolicyAsString(EnqueuePolicy v);
  static EnqueuePolicy enqueuePolicyFromString(QString v);
  QList<RequestFormField> requestFormFields() const;
  QString requestFormFieldsAsHtmlDescription() const;
  /** Create a ParamsProvider wrapper object to give access to ! pseudo params,
   * not to task params. */
  inline TaskPseudoParamsProvider pseudoParams() const;
  /** Human readable list of all triggers as one string, for UI purpose. */
  QString triggersAsString() const;
  QString triggersWithCalendarsAsString() const;
  bool triggersHaveCalendar() const;
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
  /** Workflow steps. Empty for standalone tasks. */
  QHash<QString,Step> steps() const;
  QMultiHash<QString,WorkflowTransition>
  workflowTransitionsBySourceLocalId() const;
  QHash<QString,CronTrigger> workflowCronTriggersByLocalId() const;
  /** Parent task (e.g. workflow task) to which this task belongs, if any. */
  QString workflowTaskId() const;
  void setWorkflowTask(Task workflowTask);
  QString graphvizWorkflowDiagram() const;
  PfNode toPfNode() const;
  /** to be called when activating a new configuration, to keep live attributes
   * such as lastReturnCode() or enabled() */
  void copyLiveAttributesFromOldTask(Task oldTask);
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  void changeWorkflowTransition(WorkflowTransition newItem,
                                WorkflowTransition oldItem);
  void changeStep(Step newItem, Step oldItem);
  /** Convenience method around changeStep, replacing current subtask with
   * another one.
   * As an exception to regular changeXxx() pattern, this method must only be
   * called with non-null newItem and oldItem. Moreover oldItem and newItem
   * must share the same id and a matching Step of kind Subtask must exist
   * with this id. */
  void changeSubtask(Task newItem, Task oldItem);

private:
  TaskData *data();
  const TaskData *data() const { return (const TaskData*)SharedUiItem::data(); }
};

/** ParamsProvider wrapper for pseudo params. */
class LIBQRONSHARED_EXPORT TaskPseudoParamsProvider : public ParamsProvider {
  Task _task;

public:
  inline TaskPseudoParamsProvider(Task task) : _task(task) { }
  QVariant paramValue(QString key, QVariant defaultValue = QVariant(),
                      QSet<QString> alreadyEvaluated = QSet<QString>()) const;
};

inline TaskPseudoParamsProvider Task::pseudoParams() const {
  return TaskPseudoParamsProvider(*this);
}

Q_DECLARE_TYPEINFO(Task, Q_MOVABLE_TYPE);

#endif // TASK_H
