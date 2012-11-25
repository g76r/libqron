/* Copyright 2012 Hallowyn and others.
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
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HOST_H
#define HOST_H

#include <QSharedDataPointer>

class HostData;
class PfNode;

class Host {
  QSharedDataPointer<HostData> d;

public:
  Host();
  Host(PfNode node);
  Host(const Host &other);
  ~Host();
  Host &operator=(const Host &);
  QString id() const;
  QString hostname() const;
  bool isNull() const;
};

#endif // HOST_H
