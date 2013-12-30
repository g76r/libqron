/* Copyright 2012-2013 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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

#include <QSharedData>
#include "util/paramset.h"
#include <QSet>
#include "taskgroup.h"
#include "requestformfield.h"

class TaskData;
class QDebug;
class PfNode;
class CronTrigger;
class Scheduler;
class Event;
class Step;

/** Core task definition object, being it a standalone task or workflow. */
class Task : public ParamsProvider {
  QSharedDataPointer<TaskData> d;

public:
  enum DiscardAliasesOnStart { DiscardNone, DiscardAll, DiscardUnknown };
  Task();
  Task(const Task &other);
  Task(PfNode node, Scheduler *scheduler, TaskGroup taskGroup,
       QHash<QString,Task> oldTasks, Task workflow);
  ~Task();
  Task &operator=(const Task &other);
  bool operator==(const Task &other) const;
  bool operator<(const Task &other) const;
  ParamSet params() const;
  bool isNull() const;
  /** Fully qualified task name (i.e. "taskGroupId.taskId") */
  QString id() const;
  QString fqtn() const;
  QString label() const;
  QString mean() const;
  QString command() const;
  QString target() const;
  QString info() const;
  TaskGroup taskGroup() const;
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
  QList<QRegExp> stderrFilters() const;
  void appendStderrFilter(QRegExp filter);
  void triggerStartEvents(const ParamsProvider *context) const;
  void triggerSuccessEvents(const ParamsProvider *context) const;
  void triggerFailureEvents(const ParamsProvider *context) const;
  QList<Event> onstartEvents() const;
  QList<Event> onsuccessEvents() const;
  QList<Event> onfailureEvents() const;
  /** Events hash with "onsuccess", "onfailure"... key, mainly for UI purpose.
   */
  QMultiHash<QString, Event> allEvents() const;
  bool enabled() const;
  void setEnabled(bool enabled) const;
  bool lastSuccessful() const;
  void setLastSuccessful(bool successful) const;
  int lastReturnCode() const;
  void setLastReturnCode(int code) const;
  int lastTotalMillis() const;
  void setLastTotalMillis(int lastTotalMillis) const;
  /** in millis, LLONG_MAX if not set */
  long long maxExpectedDuration() const;
  /** in millis, 0 if not set */
  long long minExpectedDuration() const;
  /** in millis, LLONG_MAX if not set */
  long long maxDurationBeforeAbort() const;
  ParamSet setenv() const;
  ParamSet unsetenv() const;
  DiscardAliasesOnStart discardAliasesOnStart() const;
  inline QString discardAliasesOnStartAsString() const {
    return discardAliasesOnStartAsString(discardAliasesOnStart()); }
  static QString discardAliasesOnStartAsString(DiscardAliasesOnStart v);
  static DiscardAliasesOnStart discardAliasesOnStartFromString(QString v);
  QList<RequestFormField> requestFormFields() const;
  QString requestFormFieldsAsHtmlDescription() const;
  QVariant paramValue(QString key, QVariant defaultValue = QVariant()) const;
  /** Human readable list of all triggers as one string, for UI purpose. */
  QString triggersAsString() const;
  QString triggersWithCalendarsAsString() const;
  bool triggersHaveCalendar() const;
  /** Cron triggers list */
  QList<CronTrigger> cronTriggers() const;
  /** Notice triggers set */
  QSet<QString> noticeTriggers() const;
  /** Human readable list of other triggers, i.e. indirect triggers such
   * as the one implied by (onsuccess(requesttask foo)) on task bar.
   * Note that not all indirect triggers can be listed here since some cannot
   * be predicted, e.g. (onsuccess(requesttask %{baz})). Only predictable ones
   * are listed. */
  QStringList otherTriggers() const;
  void appendOtherTriggers(QString text);
  void clearOtherTriggers();
  /** Workflow steps. Empty list for standalone tasks. */
  QHash<QString,Step> steps() const;
  /** Workflow task to which this task belongs, if any. */
  Task workflow() const;
};

QDebug operator<<(QDebug dbg, const Task &task);

#endif // TASK_H
