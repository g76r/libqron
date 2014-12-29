/* Copyright 2014 Hallowyn and others.
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
#include "configrepository.h"
#include "pf/pfdomhandler.h"
#include <QThread>

ConfigRepository::ConfigRepository(QObject *parent, Scheduler *scheduler)
  : QObject(parent), _scheduler(scheduler) {
  qRegisterMetaType<ConfigHistoryEntry>("ConfigHistoryEntry");
  qRegisterMetaType<QList<ConfigHistoryEntry> >("QList<ConfigHistoryEntry>");
}

SchedulerConfig ConfigRepository::parseConfig(QIODevice *source) {
  // TODO should rather have an errorString() methods or a QString *errorString param than directly logging errors
  if (!source->isOpen())
    if (!source->open(QIODevice::ReadOnly)) {
      QString errorString = source->errorString();
      Log::error() << "cannot read configuration: " << errorString;
      return SchedulerConfig();
    }
  PfDomHandler pdh;
  PfParser pp(&pdh);
  pp.parse(source);
  if (pdh.errorOccured()) {
    QString errorString = pdh.errorString()+" at line "
        +QString::number(pdh.errorLine())
        +" column "+QString::number(pdh.errorColumn());
    Log::error() << "empty or invalid configuration: " << errorString;
    return SchedulerConfig();
  }
  QList<PfNode> roots = pdh.roots();
  if (roots.size() == 0) {
    Log::error() << "configuration lacking root node";
  } else if (roots.size() == 1) {
    PfNode &root(roots.first());
    if (root.name() == "qrontab") {
      SchedulerConfig config;
      if (QThread::currentThread() == thread())
        config = parseConfig(root);
      else
        QMetaObject::invokeMethod(this, "parseConfig",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(SchedulerConfig, config),
                                  Q_ARG(PfNode, root));
      return config;
    } else {
      Log::error() << "configuration root node is not \"qrontab\"";
    }
  } else {
    Log::error() << "configuration with more than one root node";
  }
  return SchedulerConfig();
}

SchedulerConfig ConfigRepository::parseConfig(PfNode root) {
  return SchedulerConfig(root, _scheduler, true); // FIXME must not apply log config
}
