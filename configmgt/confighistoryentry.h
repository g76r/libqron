/* Copyright 2014-2023 Hallowyn and others.
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
#ifndef CONFIGHISTORYENTRY_H
#define CONFIGHISTORYENTRY_H

#include "config/schedulerconfig.h"

class ConfigHistoryEntryData;

/** Element of configuration management history */
class LIBQRONSHARED_EXPORT ConfigHistoryEntry : public SharedUiItem {
public:
  ConfigHistoryEntry();
  ConfigHistoryEntry(const ConfigHistoryEntry &other);
  ConfigHistoryEntry(
      QByteArray id, QDateTime timestamp, QString event, QByteArray configId);
  ConfigHistoryEntry &operator=(const ConfigHistoryEntry &other) {
    SharedUiItem::operator=(other); return *this; }

private:
  ConfigHistoryEntryData *data();
  const ConfigHistoryEntryData *data() const {
    return specializedData<ConfigHistoryEntryData>(); }
};

Q_DECLARE_METATYPE(ConfigHistoryEntry)
Q_DECLARE_TYPEINFO(ConfigHistoryEntry, Q_MOVABLE_TYPE);

#endif // CONFIGHISTORYENTRY_H
