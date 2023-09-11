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
#include "host.h"
#include "configutils.h"
#include "ui/qronuiutils.h"
#include "modelview/templatedshareduiitemdata.h"

class HostData : public SharedUiItemDataBase<HostData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringList _sectionNames;
  static const Utf8StringList _headerNames;
  Utf8String _id, _label, _hostname;
  QMap<Utf8String,qint64> _resources; // configured max resources available
  Utf8StringList _commentsList;
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
  d->_label = PercentEvaluator::eval_utf8(node.utf16attribute("label"),
                                          &globalParams);
  d->_hostname = ConfigUtils::sanitizeId(
        PercentEvaluator::eval_utf8(node.utf16attribute("hostname"), &globalParams),
        ConfigUtils::Hostname);
  ConfigUtils::loadResourcesSet(node, &d->_resources, "resource");
  ConfigUtils::loadComments(node, &d->_commentsList);
  setData(d);
}

Host::~Host() {
}

Utf8String Host::hostname() const {
  const HostData *d = data();
  return d ? (d->_hostname.isEmpty() ? d->_id : d->_hostname)
           : Utf8String();
}

QMap<Utf8String,qint64> Host::resources() const {
  return !isNull() ? data()->_resources : QMap<Utf8String,qint64>{};
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
        return _hostname == _id ? QVariant() : _hostname;
      return _hostname.isEmpty() ? _id : _hostname;
    case 2:
      return QronUiUtils::resourcesAsString(_resources);
    case 3:
      if (role == Qt::EditRole)
        return _label == _id ? QVariant() : _label;
      return _label.isEmpty() ? _id : _label;
    }
    break;
  default:
    ;
  }
  return QVariant();
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

void Host::detach() {
  SharedUiItem::detachedData<HostData>();
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
  foreach (const QString &key, d->_resources.keys())
    node.appendChild(
          PfNode("resource",
                 key+" "+QString::number(d->_resources.value(key))));
  return node;
}

const Utf8String HostData::_qualifier = "host";

const Utf8StringList HostData::_sectionNames {
  "hostid", // 0
  "hostname",
  "resources",
  "label",
};

const Utf8StringList HostData::_headerNames {
  "Id", // 0
  "Hostname",
  "Resources",
  "Label",
};
