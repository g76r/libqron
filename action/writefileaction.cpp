/* Copyright 2017 Hallowyn and others.
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
#include "log/log.h"
#include "config/configutils.h"
#include "sysutil/parametrizedfilewriter.h"
#include "util/paramsprovidermerger.h"

class LIBQRONSHARED_EXPORT WriteFileActionData : public ActionData {
public:
  QString _path, _message;
  ParamSet _params;

  WriteFileActionData(QString path = QString(), QString message = QString(),
                ParamSet params = ParamSet())
    : _path(path), _message(message), _params(params) {
  }
  void trigger(EventSubscription subscription, ParamSet eventContext,
               TaskInstance taskContext) const {
    Q_UNUSED(subscription)
    // LATER support binary payloads
    TaskInstancePseudoParamsProvider ppp = taskContext.pseudoParams();
    ParamsProviderMerger evaluationContext =
        ParamsProviderMerger(_params)(eventContext)(&ppp);
    ParametrizedFileWriter writer(
          _path, _params, &evaluationContext, taskContext.task().id(),
          taskContext.idAsLong());
    writer.performWrite(_message, &evaluationContext);
  }
  QString toString() const {
    return "writefile{ "+_path+" }";
  }
  QString actionType() const {
    return QStringLiteral("writefile");
  }
  PfNode toPfNode() const{
    PfNode node(actionType(), _message);
    node.appendChild(PfNode(QStringLiteral("path"), _path));
    ConfigUtils::writeParamSet(&node, _params, QStringLiteral("param"));
    return node;
  }
};

WriteFileAction::WriteFileAction(Scheduler *scheduler, PfNode node)
  : Action(new WriteFileActionData(
             node.attribute("path"), node.contentAsString(),
             ConfigUtils::loadParamSet(
               node, ParametrizedFileWriter::supportedParamNames))) {
  Q_UNUSED(scheduler)
}

WriteFileAction::WriteFileAction(const WriteFileAction &rhs) : Action(rhs) {
}
