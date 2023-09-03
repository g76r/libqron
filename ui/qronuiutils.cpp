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
#include "qronuiutils.h"
#include <QRegularExpressionMatchIterator>

Utf8String QronUiUtils::resourcesAsString(
    const QMap<Utf8String, qint64> &resources) {
  Utf8String s;
  bool first = true;
  for(auto key: resources.keys()) {
    if (first)
      first = false;
    else
      s.append(' ');
    s.append(key).append('=')
        .append(Utf8String::number(resources.value(key)));
  }
  return s;
}

static QRegularExpression keyEqualNumberRE(
      "\\s*([_a-zA-Z][_a-zA-Z0-9]*)\\s*=\\s*([0-9xXa-fA-F]+)\\s*");

bool QronUiUtils::resourcesFromString(
    const Utf8String &text, QMap<Utf8String, qint64> *resources,
    QString *errorString) {
  //qDebug() << "QronUiUtils::resourcesFromString" << text << resources
  //         << errorString;
  if (!resources) {
    if (errorString)
      *errorString = "*resources is null";
    return false;
  }
  QRegularExpressionMatchIterator i = keyEqualNumberRE.globalMatch(text);
  while (i.hasNext()) {
    QRegularExpressionMatch match = i.next();
    Utf8String key = match.captured(1);
    bool ok = false;
    qint64 value = Utf8String(match.captured(2)).toLongLong(&ok, 0);
    //qDebug() << "   match:" << key << value << ok;
    if (!ok) {
      if (errorString)
        *errorString = "cannot parse \""+match.captured()+"\"";
      resources->clear();
      return false;
    }
    resources->insert(key, value);
  }
  return true;
}
