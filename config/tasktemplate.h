/* Copyright 2021-2023 Gregoire Barbier and others.
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
#ifndef TASKTEMPLATE_H
#define TASKTEMPLATE_H

#include "task.h"

class TaskTemplateData;
class TaskOrTemplateData;

class LIBQRONSHARED_EXPORT TaskTemplate : public SharedUiItem {
  friend class Task;
public:
  TaskTemplate();
  TaskTemplate(const TaskTemplate &other);
  TaskTemplate(PfNode node, Scheduler *scheduler, SharedUiItem parent,
               QMap<Utf8String, Calendar> namedCalendars);
  TaskTemplate &operator=(const TaskTemplate &other) {
    SharedUiItem::operator=(other); return *this; }
  int maxInstances() const;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);
  QList<PfNode> originalPfNodes() const;
  PfNode toPfNode() const;

private:
  TaskTemplateData *data();
  const TaskTemplateData *data() const;
};

Q_DECLARE_TYPEINFO(TaskTemplate, Q_MOVABLE_TYPE);

#endif // TASKTEMPLATE_H
