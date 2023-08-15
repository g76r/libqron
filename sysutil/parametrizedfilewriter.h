/* Copyright 2017-2023 Hallowyn and others.
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
#ifndef PARAMETRIZEDFILEWRITER_H
#define PARAMETRIZEDFILEWRITER_H

#include "libqron_global.h"

// MAYDO support a "sync" parameter (it is not directly supported by Qt)

/** Class extending QFile to give easy ways to parametrize writing to a file
 * using ParamSet parameters.
 * Supported parameters:
 * - "truncate" QIODevice::Truncate (default: false)
 * - "append" QIODevice::Append (default: true)
 * - "unique" create a unique file name (default: false)
 *   if true, a unique file name will be generated either by replacing "XXXXXX"
 *   in file name with a random unique string or by appending it if the "XXXXXX"
 *   pattern is not found
 * - "temporary" delete file when object is deleted (default: false)
 * - "payload" to set the request data to write (ignored if performWrite()
 *   is called with a non-null payload)
 */
class LIBQRONSHARED_EXPORT ParametrizedFileWriter : public QFile {
  Q_OBJECT
  QString _rawPayloadFromParams;
  ParamSet _params;
  bool _truncate, _append, _unique, _temporary;
  QString _logTask;
  quint64 _logExecId;

public:
  explicit ParametrizedFileWriter(
      QString path, ParamSet params = ParamSet(),
      ParamsProvider *paramsEvaluationContext = 0, QString logTask = QString(),
      quint64 logExecId = 0);
  ~ParametrizedFileWriter();
  /** @param payload if not set, use "payload" parameter content instead
   * @return bytes written, or -1 on error */
  qint64 performWrite(QString payload = QString(),
                    ParamsProvider *payloadEvaluationContext = 0);
  const static QSet<QString> supportedParamNames;
};

#endif // PARAMETRIZEDFILEWRITER_H
