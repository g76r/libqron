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
#include "logfile.h"
#include "log/log.h"
#include "modelview/templatedshareduiitemdata.h"
#include "pf/pfnode.h"

static QAtomicInt _sequence;

class LogFileData : public SharedUiItemDataBase<LogFileData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  Utf8String _id;
  QString _pathPattern;
  Log::Severity _minimumSeverity;
  bool _buffered;
  LogFileData() : _id(Utf8String::number(_sequence.fetchAndAddOrdered(1))),
    _minimumSeverity(Log::Debug), _buffered(true) { }
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
};

LogFile::LogFile() {
}

LogFile::LogFile(const LogFile &other) : SharedUiItem(other) {
}

LogFile::LogFile(const PfNode &node) {
  QString pathPattern = node.attribute("file"_u8);
  if (!pathPattern.isEmpty()) {
    LogFileData *d = new LogFileData;
    d->_pathPattern = pathPattern;
    d->_minimumSeverity = p6::log::severity_from_text(node["level"_u8]);
    d->_buffered = !node.has_child("unbuffered"_u8);
    setData(d);
  }
}

LogFile::~LogFile() {
}

QString LogFile::pathPattern() const {
  const LogFileData *d = data();
  return d ? d->_pathPattern : QString();
}

Log::Severity LogFile::minimumSeverity() const {
  const LogFileData *d = data();
  return d ? d->_minimumSeverity : Log::Debug;
}

bool LogFile::buffered() const {
  const LogFileData *d = data();
  return d ? d->_buffered : false;
}

QVariant LogFileData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _pathPattern;
    case 2:
      return p6::log::severity_as_text(_minimumSeverity);
    case 3:
      return _buffered;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

LogFileData *LogFile::data() {
  return SharedUiItem::detachedData<LogFileData>();
}

const LogFileData *LogFile::data() const {
  return specializedData<LogFileData>();
}

void LogFile::detach() {
  SharedUiItem::detachedData<LogFileData>();
}

PfNode LogFile::toPfNode() const {
  const LogFileData *d = data();
  if (!d)
    return PfNode();
  PfNode node("log");
  node.append_child(PfNode("file", d->_pathPattern));
  node.append_child(
        PfNode("level", p6::log::severity_as_text(d->_minimumSeverity)));
  if (!d->_buffered)
    node.append_child(PfNode("unbuffered"));
  return node;
}

const Utf8String LogFileData::_qualifier = "logfile";

const Utf8StringIndexedConstList LogFileData::_sectionNames {
  "id", // 0
  "path_pattern",
  "minimum_severity",
  "buffered",
};

const Utf8StringIndexedConstList LogFileData::_headerNames {
  "Id", // 0
  "Path Pattern",
  "Minimum Severity",
  "Buffered",
};

