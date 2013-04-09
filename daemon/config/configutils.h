/* Copyright 2013 Hallowyn and others.
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
 * along with qron. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CONFIGUTILS_H
#define CONFIGUTILS_H

#include "pf/pfnode.h"
#include "util/paramset.h"

class ConfigUtils {
public:
  static inline bool loadParamSet(PfNode parentnode, ParamSet &params,
                                  QString &errorString) {
    return loadGenericParamSet(parentnode, params, "param", errorString); }
  static inline bool loadParamSet(PfNode parentnode, ParamSet &params) {
    QString errorString;
    return loadParamSet(parentnode, params, errorString); }
  static inline bool loadSetenv(PfNode parentnode, ParamSet &params,
                                QString &errorString) {
    return loadGenericParamSet(parentnode, params, "setenv", errorString); }
  static inline bool loadSetenv(PfNode parentnode, ParamSet &params) {
    QString errorString;
    return loadSetenv(parentnode, params, errorString); }
  static bool loadUnsetenv(PfNode parentnode, QSet<QString> &unsetenv,
                           QString &errorString);
  static bool loadUnsetenv(PfNode parentnode, QSet<QString> &unsetenv) {
    QString errorString;
    return loadUnsetenv(parentnode, unsetenv, errorString); }

private:
  static bool loadGenericParamSet(PfNode parentnode, ParamSet &params,
                                  QString attrname, QString &errorString);
  ConfigUtils();
};

#endif // CONFIGUTILS_H