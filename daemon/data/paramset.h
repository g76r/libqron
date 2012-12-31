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
 * along with qron. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef PARAMSET_H
#define PARAMSET_H

#include <QSharedData>
#include <QList>
#include <QStringList>
#include "log/log.h"

class ParamSetData;

class ParamSet {
  QSharedDataPointer<ParamSetData> d;
  ParamSet(ParamSetData *data);
public:
  ParamSet();
  ParamSet(const ParamSet &other);
  ~ParamSet();
  const ParamSet parent() const;
  ParamSet parent();
  void setParent(ParamSet parent);
  void setValue(const QString key, const QString value);
  /** Return a value without performing parameters substitution.
   * @param inherit should search values in parents if not found
   */
  QString rawValue(const QString key, bool inherit = true) const;
  /** Return a value after parameters substitution.
   * @param searchInParents should search values in parents if not found
    */
  QString value(const QString key, bool inherit = true) const {
    return evaluate(rawValue(key, inherit)); }
  /** Return a value splitted into strings after parameters substitution.
    */
  QStringList valueAsStrings(const QString key,
                             const QString separator = " ",
                             bool inherit = true) const {
    return splitAndEvaluate(rawValue(key), separator, inherit); }
  /** Return all keys for which the ParamSet or one of its parents hold a value.
    */
  const QSet<QString> keys(bool inherit = true) const;
  /** Perform parameters substitution within the string. */
  QString evaluate(const QString rawValue, bool inherit = true) const;
  /** Split string and perform parameters substitution. */
  QStringList splitAndEvaluate(const QString rawValue,
                               const QString separator = " ",
                               bool inherit = true) const;
  bool isNull() const;
  QString toString(bool inherit = true) const;

private:
  inline void appendVariableValue(QString &value, QString &variable,
                                  bool inherit = true) const;
};

QDebug operator<<(QDebug dbg, const ParamSet &params);

LogHelper operator <<(LogHelper lh, const ParamSet &params);

#endif // PARAMSET_H
