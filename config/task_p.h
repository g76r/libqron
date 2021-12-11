/* Copyright 2014-2021 Hallowyn and others.
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
#ifndef TASK_P_H
#define TASK_P_H

static QString _uiHeaderNames[] = {
  "Local Id", // 0
  "Parent Group",
  "Label",
  "Mean",
  "Command",
  "Target", // 5
  "Triggers",
  "Parameters",
  "Resources",
  "Last execution",
  "Next execution", // 10
  "Id",
  "Max instances",
  "Running count",
  "On start",
  "On success", // 15
  "On failure",
  "Running / max",
  "Actions",
  "Last execution status",
  "", // was: System environment // 20
  "Vars",
  "", // was: Unsetenv
  "Min expected duration",
  "Max expected duration",
  "Request-time overridable params", // 25
  "Last execution duration",
  "Max duration before abort",
  "Triggers incl. calendars",
  "Enabled",
  "Has triggers with calendars", // 30
  "Workflow task",
  "Last task instance id",
  "Additional info",
  "Executions count",
  "Enqueue policy" // 35
};

#endif // TASK_P_H
