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
#include "cluster.h"
#include "configutils.h"
#include "modelview/templatedshareduiitemdata.h"

class ClusterData : public SharedUiItemDataBase<ClusterData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  Utf8String _id;
  Utf8String _label;
  Cluster::Balancing _balancing;
  QList<Host> _hosts;
  Utf8StringList _commentsList;
  QVariant uiData(int section, int role) const override;
  Utf8String id() const override { return _id; }
  bool setUiData(
      int section, const QVariant &value, QString *errorString,
      SharedUiItemDocumentTransaction *transaction, int role) override;
  Qt::ItemFlags uiFlags(int section) const override;
};

Cluster::Cluster() {
}

Cluster::Cluster(const Cluster &other) : SharedUiItem(other) {
}

Cluster::Cluster(PfNode node) {
  ClusterData *d = new ClusterData;
  d->_id = ConfigUtils::sanitizeId(node.contentAsUtf16(),
                                   ConfigUtils::FullyQualifiedId).toUtf8();
  d->_label = node.utf16attribute("label");
  d->_balancing = balancingFromString(node.utf16attribute("balancing", "roundrobin")
                                      .trimmed().toLower());
  if (d->_balancing == UnknownBalancing) {
    Log::error() << "invalid cluster balancing method: " << node.toString();
    delete d;
  }
  ConfigUtils::loadComments(node, &d->_commentsList);
  setData(d);
}

Cluster::~Cluster() {
}

void Cluster::appendHost(Host host) {
  if (!isNull())
    data()->_hosts.append(host);
}

QList<Host> Cluster::hosts() const {
  return !isNull() ? data()->_hosts : QList<Host>();
}

Cluster::Balancing Cluster::balancing() const {
  return !isNull() ? data()->_balancing : UnknownBalancing;
}

void Cluster::setId(QByteArray id) {
  if (!isNull())
    data()->_id = id;
}

QVariant ClusterData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1: {
      QStringList hosts;
      for (const Host &host: _hosts)
        hosts.append(host.id());
      return hosts.join(" ");
    }
    case 2:
      return Cluster::balancingAsString(_balancing);
    case 3:
      if (role == Qt::EditRole)
        return _label == _id ? Utf8String{} : _label;
      return _label.isEmpty() ? _id : _label;
    }
    break;
  default:
    ;
  }
  return QVariant();
}

bool ClusterData::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  Q_ASSERT(transaction != 0);
  Q_ASSERT(errorString != 0);
  QString s = value.toString().trimmed();
  switch(section) {
  case 0:
    s = ConfigUtils::sanitizeId(s, ConfigUtils::FullyQualifiedId);
    _id = s.toUtf8();
    return true;
    //case 1:
    // LATER host list: parse
  case 2: {
    Cluster::Balancing balancing
        = Cluster::balancingFromString(value.toString().trimmed().toLower());
    if (balancing == Cluster::UnknownBalancing) {
      if (errorString)
        *errorString = "Unsupported balancing value: '"+value.toString()+"'";
      return false;
    }
    _balancing = balancing;
    return true;
  }
  case 3:
    _label = s.trimmed();
    return true;
  }
  return SharedUiItemData::setUiData(section, value, errorString, transaction,
                                     role);
}

Qt::ItemFlags ClusterData::uiFlags(int section) const {
  Qt::ItemFlags flags = SharedUiItemData::uiFlags(section);
  switch(section) {
  case 0:
  case 2:
  case 3:
    flags |= Qt::ItemIsEditable;
  }
  return flags;
}

bool Cluster::setUiData(
    int section, const QVariant &value, QString *errorString,
    SharedUiItemDocumentTransaction *transaction, int role) {
  if (isNull())
    return false;
  return detachedData<ClusterData>()
      ->setUiData(section, value, errorString, transaction, role);
}

ClusterData *Cluster::data() {
  return detachedData<ClusterData>();
}

const ClusterData *Cluster::data() const {
  return specializedData<ClusterData>();
}

PfNode Cluster::toPfNode() const {
  const ClusterData *d = data();
  if (!d)
    return PfNode();
  PfNode node("cluster", d->_id);
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_label.isEmpty() && d->_label != d->_id)
    node.appendChild(PfNode("label", d->_label));
  node.appendChild(PfNode("balancing", balancingAsString(d->_balancing)));
  QStringList hosts;
  for (const Host &host: d->_hosts)
    hosts.append(host.id());
  node.appendChild(PfNode("hosts", hosts.join(' ')));
  return node;
}

void Cluster::setHosts(QList<Host> hosts) {
  ClusterData *d = data();
  if (d)
    d->_hosts = hosts;
}

static QHash<Cluster::Balancing,QString> _balancingtoText {
  { Cluster::First, "first" },
  { Cluster::Each, "each" },
  { Cluster::RoundRobin, "roundrobin" },
  { Cluster::Random, "random" },
};

static QHash<QString,Cluster::Balancing> _balancingFromText {
  ContainerUtils::reversed(_balancingtoText)
};

QString Cluster::balancingAsString(Cluster::Balancing balancing) {
  return _balancingtoText.value(balancing);
}

Cluster::Balancing Cluster::balancingFromString(QString balancing) {
  return _balancingFromText.value(balancing, UnknownBalancing);
}

const Utf8String ClusterData::_qualifier = "cluster";

const Utf8StringIndexedConstList ClusterData::_sectionNames {
  "clusterid", // 0
  "hosts",
  "balancing_method",
  "label",
};

const Utf8StringIndexedConstList ClusterData::_headerNames {
  "Id", // 0
  "Hosts",
  "Balancing Method",
  "Label",
};
