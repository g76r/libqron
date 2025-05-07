/* Copyright 2024-2025 Grégoire Barbier and others.
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
#include "hostmonitor.h"
#include <QTimer>
#include "io/opensshcommand.h"
#include "alert/alerter.h"

#define ASYNC_PROCESSING_INTERVAL 10'000

HostMonitor::HostMonitor(Alerter *alerter)
  : _thread(new QThread), _alerter(alerter), _command_timeout(10'000) {
  _thread->setObjectName("HostMonitorThread");
  connect(this, &QObject::destroyed, _thread, &QThread::quit);
  connect(_thread, &QThread::finished, _thread, &QObject::deleteLater);
  _thread->start();
  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &HostMonitor::asyncProcessing);
  connect(this, &QObject::destroyed, timer, &QObject::deleteLater);
  timer->start(ASYNC_PROCESSING_INTERVAL);
  moveToThread(_thread);
}

void HostMonitor::setConfig(const SchedulerConfig &config) {
  if (this->thread() == QThread::currentThread())
    doSetConfig(config);
  else
    QMetaObject::invokeMethod(this, [this,config]() {
      doSetConfig(config);
    }, Qt::BlockingQueuedConnection);
}

void HostMonitor::doSetConfig(const SchedulerConfig &config) {
  _config = config;
  _command_timeout =
      config.params().paramNumber<double>("host.monitor.command.timeout",
                                          10.0)*1e3;
  _last_check.clear(); // check every host again
}

void HostMonitor::asyncProcessing() {
  auto now = QDateTime::currentMSecsSinceEpoch();
  for (const auto &host: _config.hosts()) {
    auto hostid = host.id();
    auto elapsed = now - _last_check[hostid];
    if (elapsed < host.healthcheckinterval())
      continue;
    auto sshhealthcheck = host.sshhealthcheck();
    if (sshhealthcheck.isEmpty())
      continue;
    auto params = host.params();
    params.insert("hostid"_u8, host.id());
    auto command = new OpensshCommand(
                     sshhealthcheck, host.hostname(), params, {},
                     "SshMonitorThread");
    command->setStandardErrorFile(QProcess::nullDevice());
    command->setStandardOutputFile(QProcess::nullDevice());
    QTimer::singleShot(_command_timeout, command, &QProcess::kill);
    connect(command, &QProcess::finished,
            this, &HostMonitor::processFinished);
    connect(command, &QProcess::errorOccurred,
            this, &HostMonitor::processError);
    command->start();
    _last_check.insert(hostid, now); // don't spam if ssh doesn't works
  }
}

void HostMonitor::processError(QProcess::ProcessError) {
  auto command = qobject_cast<OpensshCommand*>(sender());
  if (command)
    command->kill();
  processFinished(-1, QProcess::CrashExit);
}

void HostMonitor::processFinished(
    int exitCode, QProcess::ExitStatus exitStatus) {
  auto command = qobject_cast<OpensshCommand*>(sender());
  if (!command) {
    Log::warning() << "ssh healthcheck process finished without information "
                      "on checked host "_u8 << exitCode << exitStatus;
    return;
  }
  auto hostid = command->params().paramUtf8("hostid"_u8);
  auto host = _config.hosts().value(hostid);
  bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
  // Log::fatal() << "success " << hostid << " " << success << " " << exitStatus
  //              << " " << exitCode;
  _last_check.insert(hostid, QDateTime::currentMSecsSinceEpoch());
  if (success) {
    if (!host.is_available()) {
      Log::info() << "host "_u8 << hostid << " is back available"_u8;
      host.set_available(true);
      emit itemChanged(host, host, "host"_u8);
    }
    _alerter->cancelAlert("host.down."_u8+hostid);
  } else {
    if (host.is_available()) {
      Log::warning() << "host "_u8 << hostid << " is no longer available"_u8;
      host.set_available(false);
      emit itemChanged(host, host, "host"_u8);
    }
    _alerter->raiseAlert("host.down."_u8+hostid);
  }
  command->deleteLater();
}
