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
#ifndef LOCALCONFIGREPOSITORY_H
#define LOCALCONFIGREPOSITORY_H

#include "configrepository.h"
#include "config/schedulerconfig.h"
#include <QDir>
#include <QHash>
#include <QMutex>

class CsvFile;

/** ConfigRepository implementation storing config either as files in a simple
 * directory layout or (when no basePath is set) fully in memory in a
 * non-persistent way.
 */
class LIBQRONSHARED_EXPORT LocalConfigRepository : public ConfigRepository {
  Q_OBJECT
  Q_DISABLE_COPY(LocalConfigRepository)
  QByteArray _activeConfigId;
  QString _basePath;
  QHash<QByteArray,SchedulerConfig> _configs;
  CsvFile *_historyLog;
  QMutex _mutex;

public:
  LocalConfigRepository(QObject *parent, Scheduler *scheduler,
                        QString basePath = {});
  QByteArrayList availlableConfigIds() override;
  QByteArray activeConfigId() override;
  SchedulerConfig config(QByteArray id) override;
  QByteArray addConfig(SchedulerConfig config) override;
  bool activateConfig(QByteArray id) override;
  bool removeConfig(QByteArray id) override;
  void openRepository(QString basePath);

private:
  inline void recordInHistory(QString event, QByteArray configId);
};

#endif // LOCALCONFIGREPOSITORY_H
