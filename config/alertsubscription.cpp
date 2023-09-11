/* Copyright 2012-2023 Hallowyn and others.
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
#include "alertsubscription.h"
#include "alert/alertchannel.h"
#include "alert/alert.h"
#include "configutils.h"
#include "modelview/templatedshareduiitemdata.h"

static QAtomicInt _sequence;

class AlertSubscriptionData
    : public SharedUiItemDataWithImmutableParams<AlertSubscriptionData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringList _sectionNames;
  static const Utf8StringList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  Utf8String _id, _channelName;
  QString _pattern;
  QRegularExpression _patternRegexp;
  Utf8String _address, _emitMessage, _cancelMessage, _reminderMessage;
  bool _notifyEmit, _notifyCancel, _notifyReminder;
  Utf8StringList _commentsList;
  AlertSubscriptionData()
    : _id(Utf8String::number(_sequence.fetchAndAddOrdered(1))),
      _notifyEmit(false), _notifyCancel(false), _notifyReminder(false) {
  }
  Utf8String id() const override { return _id; }
  QVariant uiData(int section, int role) const override;
};

AlertSubscription::AlertSubscription() {
}

AlertSubscription::AlertSubscription(const AlertSubscription &other)
  : SharedUiItem(other) {
}

AlertSubscription::AlertSubscription(
    PfNode subscriptionnode, PfNode channelnode, ParamSet parentParams) {
  AlertSubscriptionData *d = new AlertSubscriptionData;
  d->_channelName = channelnode.utf8Name();
  d->_pattern = subscriptionnode.utf16attribute(QStringLiteral("pattern"),
                                           QStringLiteral("**"));
  d->_patternRegexp = ConfigUtils::readDotHierarchicalFilter(d->_pattern);
  if (d->_pattern.isEmpty() || !d->_patternRegexp.isValid())
    Log::warning() << "unsupported alert subscription match pattern '"
                   << d->_pattern << "': " << subscriptionnode.toString();
  d->_address = channelnode.utf16attribute("address"); // LATER check uniqueness
  d->_emitMessage = channelnode.utf16attribute("emitmessage"); // LATER check uniqueness
  d->_cancelMessage = channelnode.utf16attribute("cancelmessage"); // LATER check uniqueness
  d->_reminderMessage = channelnode.utf16attribute("remindermessage"); // LATER check uniqueness
  d->_notifyEmit = !channelnode.hasChild("noemitnotify");
  d->_notifyCancel = !channelnode.hasChild("nocancelnotify");
  d->_notifyReminder = d->_notifyEmit && !channelnode.hasChild("noremindernotify");
  d->_params = ParamSet(subscriptionnode, "param");
  d->_params << ParamSet(channelnode, "param");
  d->_params.setParent(parentParams);
  ConfigUtils::loadComments(subscriptionnode, &d->_commentsList, 0);
  ConfigUtils::loadComments(channelnode, &d->_commentsList);
  setData(d);
}

PfNode AlertSubscription::toPfNode() const {
  const AlertSubscriptionData *d = data();
  if (!d)
    return PfNode();
  PfNode subscriptionNode("subscription");
  subscriptionNode.appendChild(PfNode("pattern"_u8, d->_pattern));
  PfNode node(d->_channelName);
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_address.isEmpty())
  node.appendChild(PfNode("address"_u8, d->_address));
  if (!d->_emitMessage.isEmpty())
    node.appendChild(PfNode("emitmessage"_u8, d->_emitMessage));
  if (!d->_cancelMessage.isEmpty())
    node.appendChild(PfNode("cancelmessage"_u8,
                            d->_cancelMessage));
  if (!d->_reminderMessage.isEmpty())
    node.appendChild(PfNode("remindermessage"_u8,
                            d->_reminderMessage));
  if (!d->_notifyEmit)
    node.appendChild(PfNode("noemitnotify"_u8));
  if (!d->_notifyCancel)
    node.appendChild(PfNode("nocancelnotify"_u8));
  if (!d->_notifyReminder)
    node.appendChild(PfNode("noremindernotify"_u8));
  ConfigUtils::writeParamSet(&node, d->_params, "param");
  subscriptionNode.appendChild(node);
  return subscriptionNode;
}

QString AlertSubscription::pattern() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_pattern : QString();
}

QRegularExpression AlertSubscription::patternRegexp() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_patternRegexp : QRegularExpression();
}

Utf8String AlertSubscription::channelName() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_channelName : Utf8String();
}

Utf8String AlertSubscription::rawAddress() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_address : Utf8String();
}

Utf8String AlertSubscription::address(Alert alert) const {
  const AlertSubscriptionData *d = data();
  if (!d)
    return {};
  auto ppm = ParamsProviderMerger(d->_params)(&alert);
  return PercentEvaluator::eval_utf8(d->_address, &ppm);
}

Utf8String AlertSubscription::emitMessage(Alert alert) const {
  const AlertSubscriptionData *d = data();
  auto rawMessage = d ? d->_emitMessage : Utf8String();
  if (rawMessage.isEmpty())
    rawMessage = "alert emited: "+alert.idWithCount();
  auto ppm = ParamsProviderMerger(d->_params)(&alert);
  return PercentEvaluator::eval_utf8(rawMessage, &ppm);
}

Utf8String AlertSubscription::cancelMessage(Alert alert) const {
  const AlertSubscriptionData *d = data();
  auto rawMessage = d ? d->_cancelMessage: Utf8String();
  if (rawMessage.isEmpty())
    rawMessage = "alert canceled: "+alert.idWithCount();
  auto ppm = ParamsProviderMerger(d->_params)(&alert);
  return PercentEvaluator::eval_utf8(rawMessage, &ppm);
}

Utf8String AlertSubscription::reminderMessage(Alert alert) const {
  const AlertSubscriptionData *d = data();
  auto rawMessage = d ? d->_reminderMessage: Utf8String();
  if (rawMessage.isEmpty())
    rawMessage = "alert still raised: "+alert.idWithCount();
  auto ppm = ParamsProviderMerger(d->_params)(&alert);
  return PercentEvaluator::eval_utf8(rawMessage, &ppm);
}

bool AlertSubscription::notifyEmit() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_notifyEmit : false;
}

bool AlertSubscription::notifyCancel() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_notifyCancel : false;
}

bool AlertSubscription::notifyReminder() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_notifyReminder : false;
}

ParamSet AlertSubscription::params() const {
  const AlertSubscriptionData *d = data();
  return d ? d->_params : ParamSet();
}

QVariant AlertSubscriptionData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _pattern;
    case 2:
      return _channelName;
    case 3:
      return _address;
    case 4: {
      QString s;
      if (!_emitMessage.isEmpty())
        s.append(" emitmessage=").append(_emitMessage);
      if (!_cancelMessage.isEmpty())
        s.append(" cancelmessage=").append(_cancelMessage);
      if (!_reminderMessage.isEmpty())
        s.append(" remindermessage=").append(_reminderMessage);
      return s.mid(1);
    }
    case 5: {
      QString s;
      if (!_notifyEmit)
        s.append(" noemitnotify");
      if (!_notifyCancel)
        s.append(" nocancelnotify");
      if (!_notifyReminder)
        s.append(" noremindernotify");
      return s.mid(1);
    }
    case 6:
      return _emitMessage;
    case 7:
      return _cancelMessage;
    case 8:
      return _reminderMessage;
    case 9:
      return _notifyEmit;
    case 10:
      return _notifyCancel;
    case 11:
      return _notifyReminder;
    case 12:
      return _params.toString(false, false);
    }
    break;
  default:
    ;
  }
  return QVariant{};
}

const AlertSubscriptionData *AlertSubscription::data() const {
  return specializedData<AlertSubscriptionData>();
}

const Utf8String AlertSubscriptionData::_qualifier = "alertsubscription";

const Utf8StringList AlertSubscriptionData::_sectionNames {
  "id", // 0
  "pattern",
  "channel",
  "address",
  "messages",
  "options", // 5
  "emitmessage",
  "cancelmessage",
  "remindermessage",
  "notifyemit",
  "notifycancel", // 10
  "notifyreminder",
  "parameters"
};

const Utf8StringList AlertSubscriptionData::_headerNames {
  "Id", // 0
  "Pattern",
  "Channel",
  "Address",
  "Messages",
  "Options", // 5
  "Emit Message",
  "Cancel Message",
  "Reminder Message",
  "Notify Emit",
  "Notify Cancel", // 10
  "Notify Reminder",
  "Parameters"
};

const SharedUiItemDataFunctions AlertSubscriptionData::_paramFunctions;
