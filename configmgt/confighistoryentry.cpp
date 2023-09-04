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
#include "confighistoryentry.h"
#include "modelview/templatedshareduiitemdata.h"

class ConfigHistoryEntryData
    : public SharedUiItemDataBase<ConfigHistoryEntryData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringList _sectionNames;
  static const Utf8StringList _headerNames;
  Utf8String _id;
  QDateTime _timestamp;
  Utf8String _event, _configId;
  ConfigHistoryEntryData(
      const Utf8String &id, const QDateTime &timestamp, const Utf8String &event,
      const Utf8String &configId)
    : _id(id), _timestamp(timestamp), _event(event), _configId(configId) {
  }
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
  void setId(const Utf8String &id) { _id = id; }
};

ConfigHistoryEntry::ConfigHistoryEntry()
{
}

ConfigHistoryEntry::ConfigHistoryEntry(const ConfigHistoryEntry &other)
  : SharedUiItem(other) {
}

ConfigHistoryEntry::ConfigHistoryEntry(
    const Utf8String &id, const QDateTime &timestamp, const Utf8String &event,
    const Utf8String &configId)
  : SharedUiItem(new ConfigHistoryEntryData(id, timestamp, event, configId)) {
}

QVariant ConfigHistoryEntryData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _timestamp.toString("yyyy-MM-dd hh:mm:ss,zzz");
    case 2:
      return _event;
    case 3:
      return _configId;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

ConfigHistoryEntryData *ConfigHistoryEntry::data() {
  return detachedData<ConfigHistoryEntryData>();
}

const Utf8String ConfigHistoryEntryData::_qualifier = "confighistoryentry";

const Utf8StringList ConfigHistoryEntryData::_sectionNames {
  "historyentryid", // 0
  "timestamp",
  "event",
  "configid",
  "actions"
};

const Utf8StringList ConfigHistoryEntryData::_headerNames {
  "History Entry Id", // 0
  "Timestamp",
  "Event",
  "Config Id",
  "Actions"
};
