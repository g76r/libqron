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

static QByteArray _uiHeaderNames[] = {
  "History Entry Id", // 0
  "Timestamp",
  "Event",
  "Config Id",
  "Actions"
};

class ConfigHistoryEntryData : public SharedUiItemData {
public:
  QByteArray _id;
  QDateTime _timestamp;
  QString _event;
  QByteArray _configId;
  ConfigHistoryEntryData(
      QByteArray id, QDateTime timestamp, QString event, QByteArray configId)
    : _id(id), _timestamp(timestamp), _event(event), _configId(configId) {
  }
  QVariant uiData(int section, int role) const;
  QVariant uiHeaderData(int section, int role) const;
  int uiSectionCount() const;
  QByteArray id() const { return _id; }
  void setId(QByteArray id) { _id = id; }
  QByteArray idQualifier() const { return "confighistoryentry"_ba; }
};

ConfigHistoryEntry::ConfigHistoryEntry()
{
}

ConfigHistoryEntry::ConfigHistoryEntry(const ConfigHistoryEntry &other)
  : SharedUiItem(other) {
}

ConfigHistoryEntry::ConfigHistoryEntry(
    QByteArray id, QDateTime timestamp, QString event, QByteArray configId)
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

QVariant ConfigHistoryEntryData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int ConfigHistoryEntryData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

ConfigHistoryEntryData *ConfigHistoryEntry::data() {
  return detachedData<ConfigHistoryEntryData>();
}
