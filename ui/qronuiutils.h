/* Copyright 2014-2015 Hallowyn and others.
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
#ifndef QRONUIUTILS_H
#define QRONUIUTILS_H

#include <QString>
#include <QHash>
#include "util/paramset.h"
#include "libqron_global.h"

class LIBQRONSHARED_EXPORT QronUiUtils {
  Q_DISABLE_COPY(QronUiUtils)
  QronUiUtils() { }

public:
  static QString resourcesAsString(QHash<QString,qint64> resources);
  /** Parse a string of the form "memory=512 semaphore=1"
   * @return false on error */
  static bool resourcesFromString(
      QString text, QHash<QString,qint64> *resources, QString *errorString);
  static QString sysenvAsString(ParamSet setenv, ParamSet unsetenv);
  static QString paramsAsString(ParamSet params, bool inherit = false);
  static QString paramsKeysAsString(ParamSet params, bool inherit = false);
};

#endif // QRONUIUTILS_H
