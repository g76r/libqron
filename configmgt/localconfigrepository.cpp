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
#include "localconfigrepository.h"
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include "csv/csvfile.h"

LocalConfigRepository::LocalConfigRepository(
    QObject *parent, Scheduler *scheduler, QString basePath)
  : ConfigRepository(parent, scheduler), _historyLog(new CsvFile(this)) {
  _historyLog->setFieldSeparator(' ');
  if (!basePath.isEmpty())
    openRepository(basePath);
}

void LocalConfigRepository::openRepository(QString basePath) {
  QMutexLocker locker(&_mutex);
  _basePath = QString();
  _activeConfigId = {};
  _configs.clear();
  // create repository subdirectories if needed
  QDir configsDir(basePath+"/configs");
  QDir errorsDir(basePath+"/errors");
  if (!QDir().mkpath(configsDir.path()))
    Log::error() << "cannot access or create directory " << configsDir.path();
  if (!QDir().mkpath(errorsDir.path()))
    Log::error() << "cannot access or create directory " << errorsDir.path();
  // read active config id
  QFile activeFile(basePath+"/active");
  QByteArray activeConfigId;
  if (activeFile.open(QIODevice::ReadOnly)) {
    if (activeFile.size() < 1024) // arbitrary limit
      activeConfigId = activeFile.readAll().trimmed();
    activeFile.close();
  }
  if (activeConfigId.isEmpty()) {
    if (activeFile.exists()) {
      Log::error() << "error reading active config: " << activeFile.fileName()
                   << ((activeFile.error() == QFileDevice::NoError)
                       ? "" : " : "+activeFile.errorString());
    } else {
      Log::warning() << "config repository lacking active config: "
                     << activeFile.fileName();
    }
  }
  // read and load configs
  QFileInfoList fileInfos =
      configsDir.entryInfoList(QDir::Files|QDir::NoSymLinks,
                               QDir::Time|QDir::Reversed);
  // TODO limit number of loaded config and/or purge oldest ones and/or move oldest into an 'archive' subdir
  for (const QFileInfo &fi: fileInfos) {
    QFile f(fi.filePath());
    //qDebug() << "LocalConfigRepository::openRepository" << fi.filePath();
    if (f.open(QIODevice::ReadOnly)) {
      SchedulerConfig config = parseConfig(&f, false);
      if (config.isNull()) { // incorrect files must be removed
        Log::warning() << "moving incorect config file to 'errors' subdir: "
                       << fi.fileName();
        // remove previously existing same file in errors dir if any
        QFile(errorsDir.path()+"/"+fi.fileName()).remove();
        if (!f.rename(errorsDir.path()+"/"+fi.fileName()))
          Log::error() << "cannot move incorect config file to 'errors' "
                          "subdir: " << fi.fileName();
        continue; // ignore incorrect config files
      }
      auto configId = config.id();
      if (configId != fi.baseName()) { // filename must be fixed to match id
        auto rightPath = Utf8String(fi.path())+"/"+configId;
        Log::warning() << "renaming config file because id mismatch: "
                       << fi.fileName() << " to " << rightPath;
        // LATER also rename in config log to keep consistency
        QFile target(rightPath);
        if (target.exists()) {
          // target can already exist, since file with bad id can be a duplicate
          // remove previously existing same file in errors dir if any
          QFile(errorsDir.path()+"/"+configId.toUtf16()).remove();
          // move to errors dir
          if (!target.rename(errorsDir.path()+"/"+configId.toUtf16()))
            Log::error() << "cannot move duplicated config file to 'errors' "
                            "subdir: " << fi.fileName();
        }
        if (!f.rename(rightPath)) {
          Log::warning() << "cannot rename file: " << fi.fileName() << " to "
                         << rightPath;
        }
      }
      if (!_configs.contains(configId)) { // ignore duplicated config file
        //qDebug() << "loaded configuration successfully" << configId << fi.baseName();
        _configs.insert(configId, config);
        emit configAdded(configId, config);
      }
    } else {
      Log::error() << "cannot open file in config repository: "
                   << fi.filePath() << ": " << f.errorString();
    }
  }
  //qDebug() << "***************** contains" << activeConfigId
  //         << _configs.contains(activeConfigId) << _configs.size();
  // read and load history
  if (_historyLog->open(basePath+"/history", QIODevice::ReadWrite)) {
    if (_historyLog->header(0) != "Timestamp") {
      QStringList headers;
      headers << "Timestamp" << "Event" << "ConfigId";
      _historyLog->setHeaders(headers);
    }
    int rowCount = _historyLog->rowCount();
    int rowCountMax = 1024; // TODO parametrize
    if (rowCount > rowCountMax) { // shorten history when too long
      _historyLog->removeRows(0, rowCount-rowCountMax-1);
      rowCount = _historyLog->rowCount();
    }
    QList<ConfigHistoryEntry> history;
    for (int i = 0; i < rowCount; ++i) {
      const QStringList row = _historyLog->row(i);
      if (row.size() < 3) {
        Log::error() << "ignoring invalid config history row #" << i;
        continue;
      }
      QDateTime ts = QDateTime::fromString(row[0], Qt::ISODate);
      history.append(ConfigHistoryEntry(QByteArray::number(i), ts, row[1],
                     row[2].toUtf8()));
    }
    emit historyReset(history);
  } else {
    Log::error() << "cannot open history log: " << basePath << "/history";
  }
  _basePath = basePath;
  // activate active config
  if (_configs.contains(activeConfigId)) {
    SchedulerConfig config = _configs.value(_activeConfigId = activeConfigId);
    config.applyLogConfig();
    emit configActivated(config);
  } else {
    Log::error() << "active configuration does not exist: " << activeConfigId;
  }
}

QByteArrayList LocalConfigRepository::availlableConfigIds() {
  QMutexLocker locker(&_mutex);
  return _configs.keys();
}

QByteArray LocalConfigRepository::activeConfigId() {
  QMutexLocker locker(&_mutex);
  return _activeConfigId;
}

SchedulerConfig LocalConfigRepository::config(QByteArray id) {
  QMutexLocker locker(&_mutex);
  return _configs.value(id);
}

QByteArray LocalConfigRepository::addConfig(SchedulerConfig config) {
  QMutexLocker locker(&_mutex);
  auto id = config.id();
  if (!id.isNull()) {
    /*if (_configs.contains(id)) {
      Log::info() << "requested to add an already known config: do "
                     "nothing: " << id;
      return id;
    }*/
    if (!_basePath.isEmpty()) {
      QSaveFile f(_basePath+"/configs/"+id.toUtf16());
      auto opt = PfOptions().with_indent(2).with_payload_first()
                 .with_comments();
      if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate)
          || f.write(config.originalPfNode().as_pf(opt)) < 0
          || !f.commit()) {
        Log::error() << "error writing config in repository: " << f.fileName()
                     << ((f.error() == QFileDevice::NoError)
                         ? "" : " : "+f.errorString());
        return {};
      }
    }
    recordInHistory("addConfig", id);
    _configs.insert(id, config);
    //qDebug() << "***************** addConfig" << id;
    emit configAdded(id, config);
  }
  return id;
}

bool LocalConfigRepository::activateConfig(QByteArray id) {
  QMutexLocker locker(&_mutex);
  //qDebug() << "activateConfig" << id << _activeConfigId;
  SchedulerConfig config = _configs.value(id);
  if (config.isNull()) {
    Log::error() << "cannote activate config since it is not found in "
                    "repository: " << id;
    return false;
  }
  /*if (_activeConfigId == id) {
    Log::info() << "requested to activate the already active config: do "
                   "nothing: " << id;
    return true;
  }*/
  if (!_basePath.isEmpty()) {
    QSaveFile f(_basePath+"/active");
    if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate)
        || f.write(id+'\n') < 0
        || !f.commit()) {
      Log::error() << "error writing config in repository: " << f.fileName()
                   << ((f.error() == QFileDevice::NoError)
                       ? "" : " : "+f.errorString());
      return false;
    }
  }
  recordInHistory("activateConfig", id);
  _activeConfigId = id;
  config.applyLogConfig();
  emit configActivated(config);
  return true;
}

bool LocalConfigRepository::removeConfig(QByteArray id) {
  QMutexLocker locker(&_mutex);
  SchedulerConfig config = _configs.value(id);
  if (config.isNull()) {
    Log::error() << "cannote remove config since it is not found in "
                    "repository: " << id;
    return false;
  }
  if (id == _activeConfigId) {
    Log::error() << "cannote remove currently active config: " << id;
    return false;
  }
  if (!_basePath.isEmpty()) {
    QFile f(_basePath+"/configs/"+id);
    if (f.exists() && !f.remove()) {
      Log::error() << "error removing config from repository: " << f.fileName()
                   << ((f.error() == QFileDevice::NoError)
                       ? "" : " : "+f.errorString());
      return false;
    }
  }
  recordInHistory("removeConfig", id);
  _configs.remove(id);
  emit configRemoved(id);
  return true;
}

void LocalConfigRepository::recordInHistory(
    QString event, QByteArray configId) {
  QDateTime now = QDateTime::currentDateTime();
  ConfigHistoryEntry entry(
        QByteArray::number(_historyLog->rowCount()), now, event, configId);
  if (_historyLog->isOpen()) {
    QStringList row { now.toString(Qt::ISODate) };
    row.append(event);
    row.append(configId);
    _historyLog->appendRow(row);
  }
  emit historyEntryAppended(entry);
}
