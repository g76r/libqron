/* Copyright 2013-2024 Hallowyn and others.
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
#ifndef REQUESTFORMFIELD_H
#define REQUESTFORMFIELD_H

#include "libqron_global.h"
#include <QSharedDataPointer>
#include "io/readonlyresourcescache.h"

class RequestFormFieldData;
class PfNode;
class TaskInstance;
class ParamSet;
class Scheduler;

/** Request-time user-overridable task parameter.
 * Define an overridable task parameter along with user interface hints. */
class LIBQRONSHARED_EXPORT RequestFormField {
  QSharedDataPointer<RequestFormFieldData> d;

public:
  RequestFormField();
  RequestFormField(const RequestFormField &);
  RequestFormField(PfNode node);
  RequestFormField &operator=(const RequestFormField &);
  ~RequestFormField();
  QString toHtmlFormFragment(ReadOnlyResourcesCache *resourcesCache,
                             bool *errorOccured) const;
  QString toHtmlHumanReadableDescription() const;
  bool validate(const Utf8String &value, const Utf8String &cause) const;
  bool isNull() const;
  inline bool operator!() const { return isNull(); }
  QString id() const;
  QString format() const;
  PfNode toPfNode() const;
};

Q_DECLARE_TYPEINFO(RequestFormField, Q_MOVABLE_TYPE);

#endif // REQUESTFORMFIELD_H
