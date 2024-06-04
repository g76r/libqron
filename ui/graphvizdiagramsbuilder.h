/* Copyright 2014-2024 Hallowyn and others.
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
#ifndef GRAPHVIZDIAGRAMSBUILDER_H
#define GRAPHVIZDIAGRAMSBUILDER_H

#include "config/schedulerconfig.h"

/** Produce graphviz source for several diagrams. */
class LIBQRONSHARED_EXPORT GraphvizDiagramsBuilder {
  GraphvizDiagramsBuilder();

public:
  /** Configuration global diagrams, each one being associated in the returned
   * QHash with one of the following keys:
   * - tasksDeploymentDiagram
   * - tasksTriggerDiagram
   * - tasksResourcesHostsDiagram
   */
  static QHash<QString,QString> configDiagrams(SchedulerConfig config);
  static Utf8String herdInstanceDiagram(Scheduler *scheduler, quint64 tii);
  static Utf8String herdConfigDiagram(
      const SchedulerConfig &config, const Utf8String &taskid);
};

#endif // GRAPHVIZDIAGRAMSBUILDER_H
