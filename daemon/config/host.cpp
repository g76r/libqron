/* Copyright 2012 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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
#include <QSharedData>
#include <QString>
#include <QHash>
#include "pf/pfnode.h"
#include "log/log.h"

class HostData : public QSharedData {
  friend class Host;
  QString _id, _label, _hostname;
  QHash<QString,qint64> _resources;
public:
  HostData() { }
  HostData(const HostData &other) : QSharedData(), _id(other._id),
    _label(other._label), _hostname(other._hostname),
    _resources(other._resources) { }
};

Host::Host() : d(new HostData) {
}

Host::Host(const Host &other) : d(other.d) {
}

Host::Host(PfNode node) {
  HostData *hd = new HostData;
  hd->_id = node.attribute("id"); // LATER check uniqueness
  hd->_label = node.attribute("label", hd->_id);
  hd->_hostname = node.attribute("hostname", hd->_id);
  QListIterator<QPair<QString,qlonglong> > it(
        node.stringLongPairChildrenByName("resource"));
  while (it.hasNext()) {
    const QPair<QString,qlonglong> &p(it.next());
    if (p.second <= 0)
      Log::warning() << "ignoring resource of kind " << p.first
                     << "with incorrect quantity in host" << node.toString();
    else
      hd->_resources.insert(p.first, p.second);
  }
  d = hd;
}

Host::~Host() {
}

Host &Host::operator=(const Host &other) {
  if (this != &other)
    d.operator=(other.d);
  return *this;
}

QString Host::id() const {
  return d->_id;
}

QString Host::hostname() const {
  return d->_hostname;
}

bool Host::isNull() const {
  return d->_id.isNull();
}

QHash<QString, qint64> Host::resources() const {
  return d->_resources;
}

QString Host::resourcesAsString() const {
  QString s;
  s.append("{ ");
  if (!isNull())
    foreach(QString key, d->_resources.keys()) {
      s.append(key).append("=")
          .append(QString::number(d->_resources.value(key))).append(" ");
    }
  return s.append("}");
}
