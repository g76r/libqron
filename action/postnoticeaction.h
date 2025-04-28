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
#ifndef POSTNOTICEACTION_H
#define POSTNOTICEACTION_H

#include "action.h"

class PostNoticeActionData;
class Scheduler;

/** Action posting a notice. */
class LIBQRONSHARED_EXPORT PostNoticeAction : public Action {
public:
  explicit PostNoticeAction(Scheduler *scheduler = 0, const PfNode &node = {});
  PostNoticeAction(const PostNoticeAction &);
  ~PostNoticeAction();
};

Q_DECLARE_TYPEINFO(PostNoticeAction, Q_RELOCATABLE_TYPE);

#endif // POSTNOTICEACTION_H
