/* Copyright 2012-2024 Hallowyn and others.
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
#include "host.h"
#include "configutils.h"
#include "ui/qronuiutils.h"
#include "modelview/templatedshareduiitemdata.h"

class HostData
    : public SharedUiItemDataWithImmutableParams<HostData, false> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  static const SharedUiItemDataFunctions _paramFunctions;
  Utf8String _id, _label, _hostname, _sshhealthcheck;
  qint64 _healthcheckinterval;
  QMap<Utf8String,qint64> _resources; // configured max resources available
  mutable QAtomicInteger<bool> _is_available;
  Utf8StringList _commentsList;
  HostData() : _is_available(true) {}
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
  void setId(Utf8String id) { _id = id; }
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
};

Host::Host() {
}

Host::Host(const Host &other) : SharedUiItem(other) {
}

Host::Host(PfNode node, ParamSet globalParams) {
  HostData *d = new HostData;
  d->_id = ConfigUtils::sanitizeId(
        node.contentAsUtf16(), ConfigUtils::FullyQualifiedId).toUtf8();
  d->_params =  ParamSet(node, "param", "constparam", globalParams);
  d->_label = PercentEvaluator::eval_utf8(
                node.attribute("label"), &d->_params);
  d->_hostname = ConfigUtils::sanitizeId(
        PercentEvaluator::eval_utf8(node.attribute("hostname"), &d->_params),
        ConfigUtils::Hostname);
  d->_sshhealthcheck = node.attribute("sshhealthcheck");
  d->_healthcheckinterval =
      node["healthcheckinterval"].toNumber<double>(60)*1e3;
  ConfigUtils::loadResourcesSet(node, &d->_resources, "resource");
  ConfigUtils::loadComments(node, &d->_commentsList);
  setData(d);
}

Host::~Host() {
}

Utf8String Host::hostname() const {
  auto d = data();
  return d ? d->_hostname | d->_id : Utf8String{};
}

QMap<Utf8String,qint64> Host::resources() const {
  auto d = data();
  return d ? d->_resources : QMap<Utf8String,qint64>{};
}

void Host::set_resource(const Utf8String &key, qint64 value) {
  auto d = detachedData<HostData>();
  if (d)
    d->_resources.insert(key, value);
}

ParamSet Host::params() const {
  auto d = data();
  return d ? d->_params : ParamSet{};
}

Utf8String Host::sshhealthcheck() const {
  auto d = data();
  return d ? d->_sshhealthcheck : Utf8String{};
}

qint64 Host::healthcheckinterval() const {
  auto d = data();
  return d ? d->_healthcheckinterval : 0;
}

bool Host::is_available() const {
  auto d = data();
  return d ? d->_is_available.loadAcquire() : false;
}

void Host::set_available(bool is_available) const {
  auto d = data();
  if (d)
    d->_is_available.storeRelease(is_available);
}

QVariant HostData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      if (role == Qt::EditRole)
        return _hostname == _id ? QVariant{} : _hostname;
      return _hostname | _id;
    case 2:
      return QronUiUtils::resourcesAsString(_resources);
    case 3:
      if (role == Qt::EditRole)
        return _label == _id ? QVariant{} : _label;
      return _label | _id;
    case 4:
      return _sshhealthcheck;
    case 5:
      return _healthcheckinterval/1e3;
    case 6:
      return _is_available.loadRelaxed();
    case 7:
      return _params.toString(false, false);
    }
    break;
  default:
    ;
  }
  return {};
}

bool Host::setUiData(int section, const QVariant &value, QString *errorString,
                     SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return detachedData<HostData>()
      ->setUiData(section, value, errorString, transaction, role);
}

bool HostData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  auto s = Utf8String(value).trimmed();
  switch(section) {
  case 0:
    s = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId);
    _id = s;
    return true;
  case 1:
    _hostname = ConfigUtils::sanitizeId(s, ConfigUtils::Hostname);
    return true;
  case 2: {
    QMap<Utf8String,qint64> resources;
    if (QronUiUtils::resourcesFromString(Utf8String(value), &resources,
                                         errorString)) {
      _resources = resources;
      return true;
    }
    return false;
  }
  case 3:
    _label = s;
    return true;
  case 4:
    _sshhealthcheck = s;
    return true;
  case 5:
    _healthcheckinterval = s.toDouble()*1e3;
    return true;
  case 6:
    _is_available = s.toBool();
    return true;
  }
  return SharedUiItemData::setUiData(section, value, errorString, transaction,
                                     role);
}

Qt::ItemFlags HostData::uiFlags(int section) const {
  Qt::ItemFlags flags = SharedUiItemData::uiFlags(section);
  switch(section) {
  case 0:
  case 1:
  case 2:
  case 3:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

HostData *Host::data() {
  return SharedUiItem::detachedData<HostData>();
}

const HostData *Host::data() const {
  return specializedData<HostData>();
}

PfNode Host::toPfNode() const {
  const HostData *d = this->data();
  if (!d)
    return PfNode();
  PfNode node("host", d->_id);
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_label.isEmpty() && d->_label != d->_id)
    node.appendChild(PfNode("label", d->_label));
  if (!d->_hostname.isEmpty() && d->_hostname != d->_id)
    node.appendChild(PfNode("hostname", d->_hostname));
  if (!d->_sshhealthcheck.isEmpty())
    node.appendChild(PfNode("sshhealthcheck", d->_sshhealthcheck));
  for (auto [k,v]: d->_resources.asKeyValueRange())
    if (!k.startsWith("<maxperhost>"))
      node.appendChild(PfNode("resource", k+" "+QString::number(v)));
  return node;
}

const SharedUiItemDataFunctions HostData::_paramFunctions = {
  { "!hostid", [](const SharedUiItemData *data, const Utf8String &,
    const PercentEvaluator::EvalContext, int) -> QVariant {
      auto hd = dynamic_cast<const HostData*>(data);
      if (!hd)
        return {};
      return hd->_id;
    } },
};

const Utf8String HostData::_qualifier = "host";

const Utf8StringIndexedConstList HostData::_sectionNames {
  "hostid", // 0
  "hostname",
  "resources",
  "label",
  "sshhealthcheck",
  "healthcheckinterval", // 5
  "is_available",
  "params", // 7
};

const Utf8StringIndexedConstList HostData::_headerNames {
  "Id", // 0
  "Hostname",
  "Resources",
  "Label",
  "SSH Healthcheck",
  "Healthcheck Interval", // 5
  "Available",
  "Params", // 7
};
