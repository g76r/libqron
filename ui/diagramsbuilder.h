/* Copyright 2014-2025 Hallowyn and others.
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
#ifndef DIAGRAMSBUILDER_H
#define DIAGRAMSBUILDER_H

#include "libqron_global.h"
#include "util/paramsprovider.h"

class SchedulerConfig;
class Scheduler;

/** Produce graphviz source for several diagrams. */
class LIBQRONSHARED_EXPORT DiagramsBuilder {
  DiagramsBuilder();

public:
  /** Configuration global diagrams, each one being associated in the returned
   * QHash with one of the following keys:
   * - tasksDeploymentDiagram
   * - tasksTriggerDiagram
   * - tasksResourcesHostsDiagram
   */
  static QHash<QString,QString> configDiagrams(const SchedulerConfig &config);
  static Utf8String herdInstanceDiagram(
      Scheduler *scheduler, quint64 tii, const ParamsProvider &options);
  static Utf8String herdConfigDiagram(
      const SchedulerConfig &config, const Utf8String &taskid);
  static Utf8String taskInstanceChronogram(
      Scheduler *scheduler, quint64 tii, const ParamsProvider &options);
};

#endif // DIAGRAMSBUILDER_H
