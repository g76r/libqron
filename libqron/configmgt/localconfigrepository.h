#ifndef LOCALCONFIGREPOSITORY_H
#define LOCALCONFIGREPOSITORY_H

#include "configrepository.h"
#include "config/schedulerconfig.h"
#include <QDir>
#include <QHash>

/** ConfigRepository implementation storing config either as files in a simple
 * directory layout or (when no basePath is set) fully in memory in a
 * non-persistent way.
 */
class LIBQRONSHARED_EXPORT LocalConfigRepository : public ConfigRepository {
  Q_OBJECT
  QString _activeConfigId, _basePath;
  QHash<QString,SchedulerConfig> _configs;

public:
  LocalConfigRepository(QObject *parent, Scheduler *scheduler,
                        QString basePath = QString());
  QStringList availlableConfigIds();
  QString activeConfigId();
  SchedulerConfig config(QString id);
  QString addConfig(SchedulerConfig config);
  bool activateConfig(QString id);
  bool removeConfig(QString id);
  void openRepository(QString basePath);
};

#endif // LOCALCONFIGREPOSITORY_H
