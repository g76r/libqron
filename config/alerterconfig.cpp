/* Copyright 2014-2023 Hallowyn and others.
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
#include "alerterconfig.h"
#include "configutils.h"
#include "alert/gridboard.h"
#include "modelview/templatedshareduiitemdata.h"

#define DEFAULT_RISE_DELAY 120000 /* 120" = 2' */
// mayrise delay is disabled de facto when >= rise delay since rise delay will
// always expire before mayrise delay and the alert will become raised
#define DEFAULT_MAYRISE_DELAY DEFAULT_RISE_DELAY
#define DEFAULT_DROP_DELAY 900000 /* 900" = 15' */
#define DEFAULT_DUPLICATE_EMIT_DELAY 600000 /* 600" = 10' */
#define DEFAULT_REMIND_PERIOD 3600000 /* 3600" = 1h */
#define DEFAULT_MIN_DELAY_BETWEEN_SEND 600000 /* 600" = 10' */
#define DEFAULT_DELAY_BEFORE_FIRST_SEND 30000 /* 30" */

static QAtomicInt _sequence;

static QSet<QString> excludedDescendantsForComments {
  "subscription", "settings", "gridboard"
};

class AlerterConfigData
    : public SharedUiItemDataWithImmutableParams<AlerterConfigData, true> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  Utf8String _id;
  QList<AlertSubscription> _alertSubscriptions;
  QList<AlertSettings> _alertSettings;
  qint64 _riseDelay, _mayriseDelay, _dropDelay, _duplicateEmitDelay,
  _minDelayBetweenSend, _delayBeforeFirstSend, _remindPeriod;
  Utf8StringList _channelNames, _commentsList;
  QList<Gridboard> _gridboards;
  AlerterConfigData()
    : _id(QByteArray::number(_sequence.fetchAndAddOrdered(1))),
    _riseDelay(DEFAULT_RISE_DELAY),
    _mayriseDelay(DEFAULT_MAYRISE_DELAY), _dropDelay(DEFAULT_DROP_DELAY),
    _duplicateEmitDelay(DEFAULT_DUPLICATE_EMIT_DELAY),
    _minDelayBetweenSend(DEFAULT_MIN_DELAY_BETWEEN_SEND),
    _delayBeforeFirstSend(DEFAULT_DELAY_BEFORE_FIRST_SEND),
    _remindPeriod(DEFAULT_REMIND_PERIOD) {
  }
  AlerterConfigData(PfNode root);
  Utf8String id() const override { return _id; }
  QVariant uiData(int section, int role) const override;
};

AlerterConfig::AlerterConfig(PfNode root)
  : SharedUiItem(new AlerterConfigData(root)) {
}

AlerterConfigData::AlerterConfigData(PfNode root)
  : SharedUiItemDataWithImmutableParams<AlerterConfigData, true>(
      ParamSet(root, "param")), _riseDelay(DEFAULT_RISE_DELAY),
    _mayriseDelay(DEFAULT_MAYRISE_DELAY),
    _dropDelay(DEFAULT_DROP_DELAY),
    _duplicateEmitDelay(DEFAULT_DUPLICATE_EMIT_DELAY),
    _minDelayBetweenSend(DEFAULT_MIN_DELAY_BETWEEN_SEND),
    _delayBeforeFirstSend(DEFAULT_DELAY_BEFORE_FIRST_SEND),
    _remindPeriod(DEFAULT_REMIND_PERIOD) {
  _channelNames << "mail" << "url" << "log" << "stop";
  for (const PfNode &node: root/"settings") {
    AlertSettings settings(node);
    _alertSettings.append(settings);
    //Log::debug() << "configured alert settings " << settings.pattern() << " "
    //             << settings.patternRegexp().pattern() << " : "
    //             << settings.toPfNode().toString();
  }
  for (const PfNode &subscriptionnode: root/"subscription") {
    //Log::debug() << "found alert subscription section " << pattern << " " << stop;
    for (const PfNode &channelnode: subscriptionnode.children()) {
      if (channelnode.name() == "pattern"
          || channelnode.name() == "param"
          || channelnode.isComment()) {
        // ignore
      } else {
        if (_channelNames.contains(channelnode.utf8Name())) {
          AlertSubscription sub(subscriptionnode, channelnode, _params);
          _alertSubscriptions.append(sub);
          //Log::debug() << "configured alert subscription " << channelnode.name()
          //             << " " << sub.pattern() << " "
          //             << sub.patternRegexp().pattern();
        } else {
          Log::warning() << "alert channel '" << channelnode.name()
                         << "' unknown in alert subscription";
        }
      }
    }
  }
  _riseDelay = root["risedelay"].toDouble(DEFAULT_RISE_DELAY/1e3)*1e3;
  if (_riseDelay < 1000) // hard coded 1 second minmimum
    _riseDelay = DEFAULT_RISE_DELAY;
  _mayriseDelay = root["mayrisedelay"].toDouble(DEFAULT_MAYRISE_DELAY/1e3)*1e3;
  if (_mayriseDelay < 1000) // hard coded 1 second minmimum
    _mayriseDelay = DEFAULT_MAYRISE_DELAY;
  _dropDelay = root["dropdelay"].toDouble(DEFAULT_DROP_DELAY/1e3)*1e3;
  if (_dropDelay < 1000) // hard coded 1 second minmimum
    _dropDelay = DEFAULT_DROP_DELAY;
  _duplicateEmitDelay = root["duplicateemitdelay"]
                        .toDouble(DEFAULT_DUPLICATE_EMIT_DELAY/1e3)*1e3;
  if (_duplicateEmitDelay < 1000) // hard coded 1 second minmimum
    _duplicateEmitDelay = DEFAULT_DUPLICATE_EMIT_DELAY;
  _minDelayBetweenSend = root["mindelaybetweensend"]
                         .toDouble(DEFAULT_MIN_DELAY_BETWEEN_SEND/1e3)*1e3;
  if (_minDelayBetweenSend < 60000) // hard coded 1 minute minimum
    _minDelayBetweenSend = 60000;
  _delayBeforeFirstSend = root["delaybeforefirstsend"]
                          .toDouble(DEFAULT_DELAY_BEFORE_FIRST_SEND/1e3)*1e3;
  _remindPeriod = root["remindperiod"].toDouble(DEFAULT_REMIND_PERIOD/1e3)*1e3;
  ConfigUtils::loadComments(root, &_commentsList,
                            excludedDescendantsForComments);
  for (const PfNode &child: root/"gridboard") {
    Gridboard gridboard(child, Gridboard(), _params); // TODO load old gridboard state
    _gridboards.append(gridboard);
  }
}

ParamSet AlerterConfig::params() const {
  const AlerterConfigData *d = data();
  return d ? d->_params : ParamSet();
}

qint64 AlerterConfig::riseDelay() const {
  const AlerterConfigData *d = data();
  return d ? d->_riseDelay : DEFAULT_RISE_DELAY;
}

qint64 AlerterConfig::mayriseDelay() const {
  const AlerterConfigData *d = data();
  return d ? d->_mayriseDelay : DEFAULT_MAYRISE_DELAY;
}

qint64 AlerterConfig::dropDelay() const {
  const AlerterConfigData *d = data();
  return d ? d->_dropDelay : DEFAULT_DROP_DELAY;
}

qint64 AlerterConfig::duplicateEmitDelay() const {
  const AlerterConfigData *d = data();
  return d ? d->_duplicateEmitDelay : DEFAULT_DUPLICATE_EMIT_DELAY;
}

qint64 AlerterConfig::minDelayBetweenSend() const {
  const AlerterConfigData *d = data();
  return d ? d->_minDelayBetweenSend : DEFAULT_MIN_DELAY_BETWEEN_SEND;
}

qint64 AlerterConfig::delayBeforeFirstSend() const {
  const AlerterConfigData *d = data();
  return d ? d->_delayBeforeFirstSend
           : DEFAULT_DELAY_BEFORE_FIRST_SEND;
}

qint64 AlerterConfig::remindPeriod() const {
  const AlerterConfigData *d = data();
  return d ? d->_remindPeriod : DEFAULT_REMIND_PERIOD;
}

QList<AlertSubscription> AlerterConfig::alertSubscriptions() const {
  const AlerterConfigData *d = data();
  return d ? d->_alertSubscriptions : QList<AlertSubscription>();
}

QList<AlertSettings> AlerterConfig::alertSettings() const {
  const AlerterConfigData *d = data();
  return d ? d->_alertSettings : QList<AlertSettings>();
}

Utf8StringList AlerterConfig::channelsNames() const {
  const AlerterConfigData *d = data();
  return d ? d->_channelNames : Utf8StringList{};
}

PfNode AlerterConfig::toPfNode() const {
  const AlerterConfigData *d = data();
  if (!d)
    return PfNode();
  PfNode node("alerts");
  ConfigUtils::writeComments(&node, d->_commentsList);
  ConfigUtils::writeParamSet(&node, d->_params, "param");
  if (d->_riseDelay != DEFAULT_RISE_DELAY)
    node.setAttribute("risedelay", d->_riseDelay/1e3);
  if (d->_mayriseDelay != DEFAULT_MAYRISE_DELAY)
    node.setAttribute("mayrisedelay", d->_mayriseDelay/1e3);
  if (d->_dropDelay != DEFAULT_DROP_DELAY)
    node.setAttribute("dropdelay", d->_dropDelay/1e3);
  if (d->_duplicateEmitDelay != DEFAULT_DUPLICATE_EMIT_DELAY)
    node.setAttribute("duplicateemitdelay", d->_duplicateEmitDelay/1e3);
  if (d->_minDelayBetweenSend != DEFAULT_MIN_DELAY_BETWEEN_SEND)
    node.setAttribute("mindelaybetweensend", d->_minDelayBetweenSend/1e3);
  if (d->_delayBeforeFirstSend != DEFAULT_DELAY_BEFORE_FIRST_SEND)
    node.setAttribute("delaybeforefirstsend", d->_delayBeforeFirstSend/1e3);
  if (d->_remindPeriod != DEFAULT_REMIND_PERIOD)
    node.setAttribute("remindperiod", d->_remindPeriod/1e3);
  for (const AlertSettings &settings: d->_alertSettings)
    node.appendChild(settings.toPfNode());
  for (const AlertSubscription &sub: d->_alertSubscriptions)
    node.appendChild(sub.toPfNode());
  for (const Gridboard &gridboard: d->_gridboards)
    node.appendChild(gridboard.toPfNode());
  return node;
}

QVariant AlerterConfigData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _params.toString(false, false);
    case 2:
      return _riseDelay/1e3;
    case 3:
      return _mayriseDelay/1e3;
    case 4:
      return _dropDelay/1e3;
    case 5:
      return _minDelayBetweenSend/1e3;
    case 6:
      return _delayBeforeFirstSend/1e3;
    case 7:
      return _remindPeriod/1e3;
    case 8:
      return _duplicateEmitDelay / 1e3;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

QList<Gridboard> AlerterConfig::gridboards() const {
  const AlerterConfigData *d = data();
  return d ? d->_gridboards : QList<Gridboard>();
}

const AlerterConfigData *AlerterConfig::data() const {
  return specializedData<AlerterConfigData>();
}

const Utf8String AlerterConfigData::_qualifier = "alerterconfig";

const Utf8StringIndexedConstList AlerterConfigData::_sectionNames {
  "id", // 0
  "params",
  "risedelay",
  "mayrisedelay",
  "dropdelay",
  "mindelaybetweensend", // 5
  "delaybeforefirstsend",
  "remindperiod",
  "duplicateemitdelay"
};

const Utf8StringIndexedConstList AlerterConfigData::_headerNames {
  "Id", // 0
  "Params",
  "Rise Delay",
  "Mayrise Delay",
  "Drop Delay",
  "Delay Between Send", // 5
  "Delay Before First Send",
  "Remind Period",
  "Duplicate Emit Delay"
};

const SharedUiItemDataFunctions AlerterConfigData::_paramFunctions;
