/* Copyright 2014-2025 Hallowyn and others.
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
#include "configrepository.h"
#include "pf/pfparser.h"
#include <QThread>
#include <QIODevice>

ConfigRepository::ConfigRepository(QObject *parent, Scheduler *scheduler)
  : QObject(parent), _scheduler(scheduler) {
}

SchedulerConfig ConfigRepository::parseConfig(
    QIODevice *source, bool applyLogConfig) {
  // TODO should rather have an errorString() methods or a QString *errorString param than directly logging errors, however, warnings and error within SchedulerConfig::SchedulerConfig should also be handled
  if (!source->isOpen())
    if (!source->open(QIODevice::ReadOnly)) {
      QString errorString = source->errorString();
      Log::error() << "cannot read configuration: " << errorString;
      return SchedulerConfig();
    }
  PfParser pp;
  auto err = pp.parse(source, PfOptions().with_comments());
  if (!!err) {
    Log::error() << "empty or invalid configuration: " << err;
    return SchedulerConfig();
  }
  QList<PfNode> roots = pp.root().children_copy();
  if (roots.size() == 0) {
    Log::error() << "configuration lacking root node";
  } else if (roots.size() == 1) {
    PfNode &root(roots.first());
    if (root.name() == "config") {
      SchedulerConfig config;
      if (QThread::currentThread() == thread())
        config = SchedulerConfig(root, _scheduler, applyLogConfig);
      else
        QMetaObject::invokeMethod(this, [this,&config,root,applyLogConfig](){
          config = SchedulerConfig(root, _scheduler, applyLogConfig);
        }, Qt::BlockingQueuedConnection);
      return config;
    } else {
      Log::error() << "configuration root node is not \"config\"";
    }
  } else {
    Log::error() << "configuration with more than one root node";
  }
  return SchedulerConfig();
}
