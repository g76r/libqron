/* Copyright 2017 Hallowyn and others.
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
#ifndef WRITEFILEACTION_H
#define WRITEFILEACTION_H

#include "action.h"

/** Action writing data into a file.
 * e.g. (writefile(path /tmp/custom_file.txt)"%!taskid started\n")
 * @see ParametrizedFileWriter */
class WriteFileAction : public Action {
public:
  explicit WriteFileAction(Scheduler *scheduler = 0, PfNode node = PfNode());
  WriteFileAction(const WriteFileAction&);
};

#endif // WRITEFILEACTION_H
