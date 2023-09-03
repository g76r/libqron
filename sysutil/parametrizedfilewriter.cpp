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
#include "parametrizedfilewriter.h"

const Utf8StringSet ParametrizedFileWriter::supportedParamNames {
  "truncate", "append", "unique", "temporary", "payload" };

ParametrizedFileWriter::ParametrizedFileWriter(
    const Utf8String &path, const ParamSet &params,
    ParamsProvider *paramsEvaluationContext,
    const Utf8String &logTask, quint64 logExecId)
  : QFile(path), _params(params), _logTask(logTask), _logExecId(logExecId) {
  Q_UNUSED(paramsEvaluationContext)
  _rawPayloadFromParams = params.paramRawUtf8("payload");
  _truncate = params.paramBool("truncate", false, paramsEvaluationContext);
  _append = params.paramBool("append", true, paramsEvaluationContext);
  _unique = params.paramBool("unique", false, paramsEvaluationContext);
  _temporary = params.paramBool("temporary", false, paramsEvaluationContext);
}

ParametrizedFileWriter::~ParametrizedFileWriter() {
  if (_temporary)
    remove();
}

qint64 ParametrizedFileWriter::performWrite(
    const Utf8String &payload, ParamsProvider *payloadEvaluationContext) {
  auto effective_payload = payload.isNull() ? _rawPayloadFromParams : payload;
  auto ppm = ParamsProviderMerger(payloadEvaluationContext)(_params);
  effective_payload = PercentEvaluator::eval_utf8(effective_payload, &ppm);
  QIODevice::OpenMode mode = QIODevice::WriteOnly;
  if (_unique) {
    QTemporaryFile tf(fileName());
    if (!tf.open()) {
      Log::error(_logTask, _logExecId)
          << "cannot create unique file: " << fileName() << " error: "
          << tf.error() << " : " << tf.errorString();
      return -1;
    }
    setFileName(tf.fileName());
    tf.setAutoRemove(false);
  }
  if (_truncate)
    mode |= QIODevice::Truncate;
  if (_append)
    mode |= QIODevice::Append;
  if (!open(mode)) {
    Log::error(_logTask, _logExecId)
        << "cannot open file: " << fileName() << " error: " << error() << " : "
        << errorString();
    return -1;
  }
  // LATER support binary payloads
  qint64 bytes = write(effective_payload);
  if (bytes == -1) {
    Log::error(_logTask, _logExecId)
        << "error when writing to file: " << fileName() << " error: " << error()
        << " : " << errorString();
  } else if (bytes != effective_payload.size()) {
    Log::error(_logTask, _logExecId)
        << "error when writing to file: " << fileName() << " error: " << error()
        << " : " << errorString() << " could only write " << bytes
        << " bytes out of " << effective_payload.size();
  }
  close();
  return bytes;
}
