/* Copyright 2013-2023 Hallowyn and others.
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
#include <QSharedData>
#include "pf/pfnode.h"

static QByteArray _uiHeaderNames[] = {
  "Id", // 0
  "Path Pattern",
  "Minimum Severity",
  "Buffered",
};

static QAtomicInt _sequence;

class LogFileData : public SharedUiItemData {
public:
  QByteArray _id;
  QString _pathPattern;
  Log::Severity _minimumSeverity;
  bool _buffered;
  LogFileData() : _id(QByteArray::number(_sequence.fetchAndAddOrdered(1))),
    _minimumSeverity(Log::Debug), _buffered(true) { }
  QVariant uiData(int section, int role) const override;
  QVariant uiHeaderData(int section, int role) const override;
  int uiSectionCount() const override;
  QByteArray id() const override { return _id; }
  QByteArray idQualifier() const override { return "logfile"_ba; }
  /*bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction,
                 int role) override;
  Qt::ItemFlags uiFlags(int section) const override;*/
};

LogFile::LogFile() {
}

LogFile::LogFile(const LogFile &other) : SharedUiItem(other) {
}

LogFile::LogFile(PfNode node) {
  QString pathPattern = node.attribute(QStringLiteral("file"));
  if (!pathPattern.isEmpty()) {
    LogFileData *d = new LogFileData;
    d->_pathPattern = pathPattern;
    d->_minimumSeverity = Log::severityFromString(
          node.attribute(QStringLiteral("level")).toUtf8());
    d->_buffered = !node.hasChild(QStringLiteral("unbuffered"));
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
      return Log::severityToString(_minimumSeverity);
    case 3:
      return _buffered;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

QVariant LogFileData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int LogFileData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

LogFileData *LogFile::data() {
  return SharedUiItem::detachedData<LogFileData>();
}

void LogFile::detach() {
  SharedUiItem::detachedData<LogFileData>();
}

PfNode LogFile::toPfNode() const {
  const LogFileData *d = data();
  if (!d)
    return PfNode();
  PfNode node("log");
  node.appendChild(PfNode("file", d->_pathPattern));
  node.appendChild(PfNode("level", Log::severityToString(d->_minimumSeverity)));
  if (!d->_buffered)
    node.appendChild(PfNode("unbuffered"));
  return node;
}
