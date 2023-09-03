/* Copyright 2013-2023 Hallowyn and others.
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
#include "postnoticeaction.h"
#include "action_p.h"
#include "config/configutils.h"

class PostNoticeActionData : public ActionData {
public:
  QString _notice;
  ParamSet _noticeParams;
  PostNoticeActionData(Scheduler *scheduler = 0, QString notice = QString(),
                       ParamSet params = ParamSet())
    : ActionData(scheduler), _notice(notice), _noticeParams(params) { }
  Utf8String toString() const override {
    return "^"+_notice;
  }
  Utf8String actionType() const override {
    return "postnotice"_u8;
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance) const override {
    if (!_scheduler)
      return;
    ParamSet noticeParams;
    for (auto key: _noticeParams.paramKeys())
      noticeParams.setValue(key, _noticeParams.paramValue(key, context));
    _scheduler->postNotice(PercentEvaluator::eval_utf8(_notice, context),
                           noticeParams);
  }
  Utf8String targetName() const override {
    return _notice;
  }
  ParamSet params() const override {
    return _noticeParams;
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _notice);
    ConfigUtils::writeParamSet(&node, _noticeParams, "param");
    return node;
  }
};

PostNoticeAction::PostNoticeAction(Scheduler *scheduler, PfNode node)
  : Action(new PostNoticeActionData(scheduler, node.contentAsUtf16(),
                                    ParamSet(node, "param"))) {
}

PostNoticeAction::PostNoticeAction(const PostNoticeAction &rhs) : Action(rhs) {
}

PostNoticeAction::~PostNoticeAction() {
}
