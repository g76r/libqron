/* Copyright 2014-2016 Hallowyn and others.
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
#ifndef ACCESSCONTROLCONFIG_H
#define ACCESSCONTROLCONFIG_H

#include "libqron_global.h"
#include <QSharedDataPointer>
#include "pf/pfnode.h"

class AccessControlConfigData;
class InMemoryAuthenticator;
class InMemoryUsersDatabase;
class QFileSystemWatcher;

// TODO convert to SharedUiItem
class LIBQRONSHARED_EXPORT AccessControlConfig {
  QSharedDataPointer<AccessControlConfigData> d;

public:
  AccessControlConfig();
  AccessControlConfig(const AccessControlConfig &);
  explicit AccessControlConfig(PfNode node);
  AccessControlConfig &operator=(const AccessControlConfig &);
  ~AccessControlConfig();
  PfNode toPfNode() const;
  void applyConfiguration(InMemoryAuthenticator *authenticator,
                          InMemoryUsersDatabase *usersDatabase,
                          QFileSystemWatcher *accessControlFilesWatcher) const;
  bool isEmpty() const;
};

Q_DECLARE_TYPEINFO(AccessControlConfig, Q_MOVABLE_TYPE);

#endif // ACCESSCONTROLCONFIG_H
