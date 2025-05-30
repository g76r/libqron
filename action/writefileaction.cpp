/* Copyright 2017-2025 Hallowyn and others.
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
#include "writefileaction.h"
#include "action_p.h"
#include "config/configutils.h"
#include "sysutil/parametrizedfilewriter.h"

class LIBQRONSHARED_EXPORT WriteFileActionData : public ActionData {
public:
  QString _path, _message;
  ParamSet _params;

  WriteFileActionData(QString path = QString(), QString message = QString(),
                ParamSet params = ParamSet())
    : _path(path), _message(message), _params(params) {
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    context->prepend(_params);
    // LATER support binary payloads
    auto path = PercentEvaluator::eval_utf8(_path, context);
    ParametrizedFileWriter writer(
          path, _params, context, instance.taskId(), instance.idAsLong());
    writer.performWrite(_message, context);
    context->pop_front();
  }
  Utf8String toString() const override {
    return "writefile{ "+_path+" }";
  }
  Utf8String actionType() const override {
    return "writefile"_u8;
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _message);
    node.append_child({"path"_u8, _path});
    ConfigUtils::writeParamSet(&node, _params, QStringLiteral("param"));
    return node;
  }
};

WriteFileAction::WriteFileAction(Scheduler *scheduler, const PfNode &node)
  : Action(new WriteFileActionData(
      node["path"], node.content_as_text(),
      ParamSet(node, ParametrizedFileWriter::supportedParamNames))) {
  Q_UNUSED(scheduler)
}

WriteFileAction::WriteFileAction(const WriteFileAction &rhs) : Action(rhs) {
}
