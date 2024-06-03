/* Copyright 2024 Grégoire Barbier and others.
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
#ifndef HOSTMONITOR_H
#define HOSTMONITOR_H

#include <QThread>
#include <QProcess>
#include "config/schedulerconfig.h"

class Alerter;

class HostMonitor : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY(HostMonitor)
  QThread *_thread;
  Alerter *_alerter;
  SchedulerConfig _config;
  QMap<Utf8String, qint64> _last_check;
  int _command_timeout;

public:
  HostMonitor(Alerter *alerter);
  void setConfig(const SchedulerConfig &config);

signals:
  void itemChanged(const SharedUiItem &new_item, const SharedUiItem &old_item,
                   const Utf8String &qualifier);

private slots:
  void asyncProcessing();
  void processError(QProcess::ProcessError error);
  void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
  Q_INVOKABLE void doSetConfig(const SchedulerConfig &config);
};

#endif // HOSTMONITOR_H
