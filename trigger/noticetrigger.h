/* Copyright 2013-2025 Hallowyn and others.
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
#ifndef NOTICETRIGGER_H
#define NOTICETRIGGER_H

#include "trigger.h"

class NoticeTriggerData;

class LIBQRONSHARED_EXPORT NoticeTrigger : public Trigger {
public:
  NoticeTrigger();
  NoticeTrigger(const PfNode &node,
                const QMap<Utf8String, Calendar> &namedCalendars);
  NoticeTrigger(const NoticeTrigger &);
  NoticeTrigger &operator=(const NoticeTrigger &);
  ~NoticeTrigger();
};

Q_DECLARE_TYPEINFO(NoticeTrigger, Q_MOVABLE_TYPE);

#endif // NOTICETRIGGER_H
