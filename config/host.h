/* Copyright 2012-2025 Hallowyn and others.
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
#ifndef HOST_H
#define HOST_H

#include "libqron_global.h"
#include "modelview/shareduiitem.h"

class HostData;
class PfNode;

/** A host is a single execution target.
 * @see Cluster */
class LIBQRONSHARED_EXPORT Host : public SharedUiItem {
public:
  Host();
  Host(const PfNode &node, const ParamSet &globalParams);
  Host(const Host &other);
  ~Host();
  Host &operator=(const Host &other) {
    SharedUiItem::operator=(other); return *this; }
  Utf8String hostname() const;
  /** Configured max resources available. */
  QMap<Utf8String, qint64> resources() const;
  void set_resource(const Utf8String &key, qint64 value);
  ParamSet params() const;
  /** healthcheck ssh command
   *  default: no healthcheck
   *  examples: true, /bin/true */
  Utf8String sshhealthcheck() const;
  /** healthcheck interval (milliseconds)
   *  default: 60'000 */
  qint64 healthcheckinterval() const;
  /** thread-safe */
  bool is_available() const;
  /** thread-safe */
  void set_available(bool is_available) const;
  PfNode toPfNode() const;
  bool setUiData(int section, const QVariant &value, QString *errorString,
                 SharedUiItemDocumentTransaction *transaction, int role);

private:
  inline HostData *data();
  inline const HostData *data() const;
};

Q_DECLARE_TYPEINFO(Host, Q_MOVABLE_TYPE);

#endif // HOST_H
