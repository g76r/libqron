/* Copyright 2022-2023 Gregoire Barbier and others.
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
#include "paramappendaction.h"
#include "action_p.h"
#include "sched/taskinstance.h"

class ParamAppendActionData : public ActionData {
public:
  QString _key, _rawvalue;
  ParamAppendActionData(QString key, QString rawvalue)
    : _key(key), _rawvalue(rawvalue) { }
  ParamAppendActionData()
    : ParamAppendActionData(QString(), QString()) { }
  ParamAppendActionData(QStringList twoStringsList)
    : ParamAppendActionData(twoStringsList.value(0),
                            twoStringsList.value(1)) { }
  QString toString() const {
    return "paramappend{ "+_key+" += "+_rawvalue+" }";
  }
  QString actionType() const {
    return QStringLiteral("paramappend");
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const {
    if (instance.isNull())
      return;
    QString value = ParamSet().evaluate(_rawvalue, context);
    instance.paramAppend(_key, value);
  }
  PfNode toPfNode() const{
    PfNode node(actionType(), _key+" "+_rawvalue);
    return node;
  }
};

ParamAppendAction::ParamAppendAction(Scheduler*, PfNode node)
  : Action(new ParamAppendActionData(node.contentAsTwoStringsList())) {
}

ParamAppendAction::ParamAppendAction(const ParamAppendAction &rhs)
 : Action(rhs) {
}

ParamAppendAction::~ParamAppendAction() {
}
