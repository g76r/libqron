/* Copyright 2012-2016 Hallowyn and others.
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
#include "alerter.h"
#include <QThread>
#include <QtDebug>
#include <QMetaObject>
#include "log/log.h"
#include "pf/pfnode.h"
#include "logalertchannel.h"
#include "urlalertchannel.h"
#include "mailalertchannel.h"
#include <QTimer>
#include <QCoreApplication>
#include "config/configutils.h"
#include "alert.h"
#include "thread/circularbuffer.h"

// LATER replace this 10" ugly batch with predictive timer (min(timestamps))
// taking into account that it must be at less as precise as visibilitywindow
#define ASYNC_PROCESSING_INTERVAL 10000

static int staticInit() {
  qMetaTypeId<QList<AlertSubscription>>();
  qMetaTypeId<AlertSubscription>();
  qMetaTypeId<AlertSettings>();
  qMetaTypeId<Alert>();
  qMetaTypeId<AlerterConfig>();
  qMetaTypeId<ParamSet>();
  return 0;
}
Q_CONSTRUCTOR_FUNCTION(staticInit)

class GridboardThread : public QThread {
  friend class Alerter;
  Q_DISABLE_COPY(GridboardThread)
  Alerter *_alerter;
  CircularBuffer<Alert> _buffer;
  qint64 _gridboardsEvaluationsCounter, _gridboardsUpdatesCounter;

public:
  GridboardThread(Alerter *alerter) : _alerter(alerter), _buffer(10),
  _gridboardsEvaluationsCounter(0), _gridboardsUpdatesCounter(0) { }
  void run() {
    while (!isInterruptionRequested()) {
      Alert alert;
      if (_buffer.tryGet(&alert, 500)) {
        ++_gridboardsEvaluationsCounter;
        QList<Gridboard> &gridboards = _alerter->_gridboards.lockData();
        for (int i = 0; i < gridboards.size(); ++i) {
          QRegularExpressionMatch match =
              gridboards[i].patternRegexp().match(alert.id());
          if (match.hasMatch()) {
            ++_gridboardsUpdatesCounter;
            gridboards[i].update(match, alert);
          }
        }
        _alerter->_gridboards.unlockData();
      }
    }
  }
};

Alerter::Alerter() : QObject(0), _alerterThread(new QThread),
  _gridboardThread(new GridboardThread(this)),
  _emitRequestsCounter(0), _raiseRequestsCounter(0), _cancelRequestsCounter(0),
  _raiseImmediateRequestsCounter(0), _cancelImmediateRequestsCounter(0),
  _emitNotificationsCounter(0), _raiseNotificationsCounter(0),
  _cancelNotificationsCounter(0), _totalChannelsNotificationsCounter(0),
  _rulesCacheSize(0), _rulesCacheHwm(0), _deduplicatingAlertsCount(0),
  _deduplicatingAlertsHwm(0) {
  _alerterThread->setObjectName("AlerterThread");
  connect(_alerterThread, &QThread::finished,
          _alerterThread, &QThread::deleteLater);
  _alerterThread->start();
  _gridboardThread->setObjectName("GridboardThread");
  connect(_gridboardThread, &GridboardThread::finished,
          _gridboardThread, &GridboardThread::deleteLater);
  _gridboardThread->start();
  _channels.insert("log", new LogAlertChannel(this));
  _channels.insert("url", new UrlAlertChannel(this));
  _channels.insert("mail", new MailAlertChannel(this));
  for (AlertChannel *channel : _channels)
    connect(this, &Alerter::configChanged,
            channel, &AlertChannel::setConfig,
            Qt::BlockingQueuedConnection);
  QTimer *timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, &Alerter::asyncProcessing);
  timer->start(ASYNC_PROCESSING_INTERVAL);
  moveToThread(_alerterThread);
}

Alerter::~Alerter() {
  // delete channels
  foreach (AlertChannel *channel, _channels.values())
    channel->deleteLater();
  // stop and delete alerter thread
  _alerterThread->quit();
  // stop and delete gridboard thread
  _gridboardThread->_buffer.clear();
  _gridboardThread->requestInterruption();
  _gridboardThread->wait();
}

void Alerter::setConfig(AlerterConfig config) {
  if (this->thread() == QThread::currentThread())
    doSetConfig(config);
  else
    QMetaObject::invokeMethod(this, "doSetConfig", Qt::BlockingQueuedConnection,
                              Q_ARG(AlerterConfig, config));
}

void Alerter::doSetConfig(AlerterConfig config) {
  AlerterConfig oldConfig = _config;
  _rulesCacheHwm = qMax(_rulesCacheHwm, qMax(_alertSubscriptionsCache.size(),
                                             _alertSettingsCache.size()));
  _rulesCacheSize = 0;
  _alertSubscriptionsCache.clear();
  _alertSettingsCache.clear();
  _config = config;
  _gridboards = config.gridboards(); // LATER merge with current data
  // LATER recompute visibilityDate and cancellationDate in statefulAlerts and emittedAlerts
  emit paramsChanged(_config.params(), oldConfig.params(), "alertparams");
  emit configChanged(_config);
}

void Alerter::emitAlert(QString alertId) {
  QMetaObject::invokeMethod(this, "doEmitAlert", Q_ARG(QString, alertId));
}

void Alerter::raiseAlert(QString alertId) {
  QMetaObject::invokeMethod(this, "doRaiseAlert", Q_ARG(QString, alertId),
                            Q_ARG(bool, false));
}

void Alerter::raiseAlertImmediately(QString alertId) {
  QMetaObject::invokeMethod(this, "doRaiseAlert", Q_ARG(QString, alertId),
                            Q_ARG(bool, true));
}

void Alerter::cancelAlert(QString alertId) {
  QMetaObject::invokeMethod(this, "doCancelAlert", Q_ARG(QString, alertId),
                            Q_ARG(bool, false));
}

void Alerter::cancelAlertImmediately(QString alertId) {
  QMetaObject::invokeMethod(this, "doCancelAlert", Q_ARG(QString, alertId),
                            Q_ARG(bool, true));
}

void Alerter::doRaiseAlert(QString alertId, bool immediately) {
  Alert oldAlert = _statefulAlerts.value(alertId);
  Alert newAlert = oldAlert.isNull() ? Alert(alertId) : oldAlert;
  QDateTime now = QDateTime::currentDateTime();
  CronTrigger acceptabilityWindow =
      alertSettings(alertId).acceptabilityWindow();
  if (acceptabilityWindow.isValid() && !acceptabilityWindow.isTriggering(now)) {
    Log::debug() << "ignoring alert out of acceptability window: " << alertId;
    return;
  }
  ++_raiseRequestsCounter;
//  if (alertId.startsWith("task.maxinstancesreached")
//      || alertId.startsWith("task.maxinstancesreached")) {
//    qDebug() << "doRaiseAlert:" << alertId << immediately << oldAlert.statusToString();
//  }
  if (immediately) {
    ++_raiseImmediateRequestsCounter;
    switch (oldAlert.status()) {
    case Alert::Rising:
    case Alert::Nonexistent:
    case Alert::Canceled: // should not happen
    case Alert::MayRise:
      actionRaise(&newAlert);
      break;
    case Alert::Dropping:
      actionNoLongerCancel(&newAlert);
      break;
    case Alert::Raised:
      goto nothing_changed;
    }
  } else {
    switch (oldAlert.status()) {
    case Alert::Rising:
    case Alert::Raised:
      goto nothing_changed;
    case Alert::MayRise:
      if (newAlert.visibilityDate() <= now) {
        actionRaise(&newAlert);
        break;
      }
      Q_FALLTHROUGH();
    case Alert::Nonexistent:
    case Alert::Canceled: // should not happen
      newAlert.setVisibilityDate(
            windowCorrectedVisibilityDate(
              alertId, newAlert.riseDate().addMSecs(riseDelay(alertId))));
      newAlert.setStatus(Alert::Rising);
      break;
    case Alert::Dropping:
      actionNoLongerCancel(&newAlert);
      break;
    }
  }
 commitChange(&newAlert, &oldAlert);
nothing_changed:
 if (newAlert.isNull()) // gridboards need an id to identify canceled alert
   newAlert = Alert(alertId); // TODO not sure it is usefull for raise
 notifyGridboards(newAlert);
}

void Alerter::doCancelAlert(QString alertId, bool immediately) {
  Alert oldAlert = _statefulAlerts.value(alertId), newAlert = oldAlert;
  ++_cancelRequestsCounter;
//  if (alertId.startsWith("task.maxinstancesreached")
//      || alertId.startsWith("task.maxinstancesreached")) {
//    qDebug() << "doCancelAlert:" << alertId << immediately << oldAlert.statusToString();
//  }
  if (immediately) {
    ++_cancelImmediateRequestsCounter;
    switch (oldAlert.status()) {
    case Alert::Nonexistent:
    case Alert::Canceled: // should not happen
      goto nothing_changed;
    case Alert::Rising:
    case Alert::MayRise:
      newAlert = Alert();
      break;
    case Alert::Raised:
    case Alert::Dropping:
      newAlert.setCancellationDate(QDateTime::currentDateTime());
      actionCancel(&newAlert);
      break;
    }
  } else {
    switch (oldAlert.status()) {
    case Alert::Nonexistent:
    case Alert::MayRise:
    case Alert::Dropping:
    case Alert::Canceled: // should not happen
      goto nothing_changed;
    case Alert::Rising:
      newAlert.setStatus(Alert::MayRise);
      newAlert.setCancellationDate(QDateTime::currentDateTime().addMSecs(
                                     mayriseDelay(alertId)));
      break;
    case Alert::Raised:
      newAlert.setStatus(Alert::Dropping);
      newAlert.setCancellationDate(QDateTime::currentDateTime().addMSecs(
                                     mayriseDelay(alertId)));
    }
  }
  commitChange(&newAlert, &oldAlert);
nothing_changed:
  if (newAlert.isNull()) // gridboards need an id to identify canceled alert
    newAlert = Alert(alertId);
  notifyGridboards(newAlert);
}

void Alerter::doEmitAlert(QString alertId) {
  ++_emitRequestsCounter;
  Alert alert = _oneshotAlerts.value(alertId);
  QDateTime now = QDateTime::currentDateTime();
  CronTrigger acceptabilityWindow =
      alertSettings(alertId).acceptabilityWindow();
  if (acceptabilityWindow.isValid() && !acceptabilityWindow.isTriggering(now)) {
    Log::debug() << "ignoring alert out of acceptability window: " << alertId;
    return;
  }
  Log::debug() << "emit one-shot alert: " << alertId;
  if (alert.isNull()) {
    // alert not emitted recently
    CronTrigger visibilityWindow = alertSettings(alertId).visibilityWindow();
    alert = Alert(alertId);
    if (!visibilityWindow.isValid() || visibilityWindow.isTriggering(now)) {
      // inside visibility window: notify channels and record in emittedAlerts
      notifyChannels(alert);
      alert.resetCount(); // record with count = 0
    } else {
      // outside visibility window: record in emittedAlerts without notifying
    }
    alert.setVisibilityDate(
          windowCorrectedVisibilityDate(
            alertId, now.addMSecs(duplicateEmitDelay(alertId))));
    _oneshotAlerts.insert(alertId, alert);
    ++_deduplicatingAlertsCount;
    _deduplicatingAlertsHwm = qMax(_deduplicatingAlertsCount,
                                   _deduplicatingAlertsHwm);
  } else if (alert.visibilityDate() >= now) {
    // alert already emitted recently : increment counter and don't notify
    alert.incrementCount();
    _oneshotAlerts.insert(alertId, alert);
  } else {
    // alert emitted long ago enough to be notified again or window reached
    alert.incrementCount();
    alert.setRiseDate(now);
    notifyChannels(alert);
    alert.resetCount();
    alert.setVisibilityDate(
          windowCorrectedVisibilityDate(
            alertId, now.addMSecs(duplicateEmitDelay(alertId))));
    _oneshotAlerts.insert(alertId, alert);
  }
}

void Alerter::asyncProcessing() {
  QDateTime now = QDateTime::currentDateTime();
  foreach (Alert oldAlert, _statefulAlerts) {
    switch(oldAlert.status()) {
    case Alert::Nonexistent: // should never happen
    case Alert::Canceled: // should never happen
    case Alert::Raised:
      break; // nothing to do
    case Alert::MayRise:
      // following triple-if is needed to determine which one among visibility
      // and cancellation dates was reached first (if one has even been reached)
      // because asyncProcessing is not called often enough to avoid conflicts
      // between these two events
      if (oldAlert.cancellationDate() <= now
          && oldAlert.cancellationDate() < oldAlert.visibilityDate()) {
        Alert newAlert;
        commitChange(&newAlert, &oldAlert);
      } else if (oldAlert.visibilityDate() <= now) {
        Alert newAlert = oldAlert;
        actionRaise(&newAlert);
        commitChange(&newAlert, &oldAlert);
      } else if (oldAlert.cancellationDate() <= now) {
        Alert newAlert;
        commitChange(&newAlert, &oldAlert);
      }
      break;
    case Alert::Rising:
      if (oldAlert.visibilityDate() <= now) {
        Alert newAlert = oldAlert;
        actionRaise(&newAlert);
        commitChange(&newAlert, &oldAlert);
        break;
      }
      break;
    case Alert::Dropping:
      if (oldAlert.cancellationDate() <= now) {
        Alert newAlert = oldAlert;
        actionCancel(&newAlert);
        commitChange(&newAlert, &oldAlert);
      }
      break;
    }
  }
  foreach (Alert alert, _oneshotAlerts) {
    if (alert.visibilityDate() <= now) {
      if (alert.count() > 0) {
        notifyChannels(alert);
      }
      _oneshotAlerts.remove(alert.id());
      --_deduplicatingAlertsCount;
    }
  }
  _rulesCacheSize = qMax(_alertSubscriptionsCache.size(),
                         _alertSettingsCache.size());
  _rulesCacheHwm = qMax(_rulesCacheHwm, _rulesCacheSize);
  if (_rulesCacheSize > 1000) {
    // clear rules cache if too large, just to avoid memory overflow if a huge
    // number of different alerts occurs
    // LATER have a nicer LRU-or-the-like cache algorithm
    _alertSubscriptionsCache.clear();
    _alertSettingsCache.clear();
    _rulesCacheSize = 0;
  }
}

void Alerter::actionRaise(Alert *newAlert) {
  newAlert->setStatus(Alert::Raised);
  newAlert->setVisibilityDate(QDateTime());
  Log::debug() << "alert raised: " << newAlert->id();
  notifyChannels(*newAlert);
}

void Alerter::actionCancel(Alert *newAlert) {
  newAlert->setStatus(Alert::Canceled);
  Log::debug() << "do cancel alert: " << newAlert->id();
  notifyChannels(*newAlert);
}

void Alerter::actionNoLongerCancel(Alert *newAlert) {
  newAlert->setStatus(Alert::Raised);
  newAlert->setCancellationDate(QDateTime());
  Log::debug() << "alert is no longer scheduled for cancellation "
               << newAlert->id()
               << " (it was raised again within drop delay)";
}

void Alerter::notifyChannels(Alert newAlert) {
  switch(newAlert.status()) {
  case Alert::Raised:
    ++_raiseNotificationsCounter;
    break;
  case Alert::Canceled:
    ++_cancelNotificationsCounter;
    break;
  case Alert::Nonexistent:
    ++_emitNotificationsCounter;
    break;
  default: // should never happen
    ;
  }
  Alert newAlertWithoutSubscription = newAlert;
  foreach (const AlertSubscription &sub, alertSubscriptions(newAlert.id())) {
    AlertChannel *channel = _channels.value(sub.channelName());
    if (channel) { // should never be false
      ++_totalChannelsNotificationsCounter;
      newAlert.setSubscription(sub);
      channel->notifyAlert(newAlert);
    }
  }
  emit alertNotified(newAlertWithoutSubscription);
}

void Alerter::notifyGridboards(Alert newAlert) {
  _gridboardThread->_buffer.tryPut(newAlert, 10);
}

void Alerter::commitChange(Alert *newAlert, Alert *oldAlert) {
  switch (newAlert->status()) {
  case Alert::Nonexistent: // should not happen
  case Alert::Canceled:
    _statefulAlerts.remove(oldAlert->id());
    *newAlert = Alert();
    break;
  default:
    _statefulAlerts.insert(newAlert->id(), *newAlert);
  }
//  if (newAlert->id().startsWith("task.maxinstancesreached")
//      || oldAlert->id().startsWith("task.maxinstancesreached")) {
//    qDebug() << "commitChange:" << newAlert->id() << newAlert->statusToString() << oldAlert->statusToString();
//  }
  emit statefulAlertChanged(*newAlert, *oldAlert, QStringLiteral("alert"));
}

QList<AlertSubscription> Alerter::alertSubscriptions(QString alertId) {
  if (!_alertSubscriptionsCache.contains(alertId)) {
    QList<AlertSubscription> list;
    foreach (const AlertSubscription &sub, _config.alertSubscriptions()) {
      if (sub.patternRegexp().match(alertId).hasMatch()) {
        QString channelName = sub.channelName();
        if (channelName == QStringLiteral("stop"))
          break;
        if (!_channels.contains(channelName)) // should never happen
          continue;
        list.append(sub);
      }
    }
    _alertSubscriptionsCache.insert(alertId, list);
  }
  return _alertSubscriptionsCache.value(alertId);
}

AlertSettings Alerter::alertSettings(QString alertId) {
  if (!_alertSettingsCache.contains(alertId)) {
    foreach (const AlertSettings &settings, _config.alertSettings()) {
      if (settings.patternRegexp().match(alertId).hasMatch()) {
        _alertSettingsCache.insert(alertId, settings);
        goto found;
      }
    }
    _alertSettingsCache.insert(alertId, AlertSettings());
found:;
    //Log::debug() << "adding settings to cache: " << alertId << " "
    //             << _alertSettingsCache.value(alertId).toPfNode().toString();
  }
  return _alertSettingsCache.value(alertId);
}

qint64 Alerter::riseDelay(QString alertId) {
  qint64 delay = alertSettings(alertId).riseDelay();
  return delay > 0 ? delay : _config.riseDelay();
}

qint64 Alerter::mayriseDelay(QString alertId) {
  qint64 delay = alertSettings(alertId).mayriseDelay();
  return delay > 0 ? delay : _config.mayriseDelay();
}

qint64 Alerter::dropDelay(QString alertId) {
  qint64 delay = alertSettings(alertId).dropDelay();
  return delay > 0 ? delay : _config.dropDelay();
}

qint64 Alerter::duplicateEmitDelay(QString alertId) {
  qint64 delay = alertSettings(alertId).duplicateEmitDelay();
  return delay > 0 ? delay : _config.duplicateEmitDelay();
}

Gridboard Alerter::gridboard(QString gridboardId) const {
  foreach (const Gridboard &gridboard, _gridboards.data())
    if (gridboard.id() == gridboardId)
      return gridboard;
  return Gridboard();
}

void Alerter::clearGridboard(QString gridboardId) {
  QList<Gridboard> &gridboards = _gridboards.lockData();
  for (int i = 0; i < gridboards.size(); ++i)
    if (gridboards[i].id() == gridboardId) {
      gridboards[i].clear();
      break;
    }
  _gridboards.unlockData();
}

qint64 Alerter::gridboardsEvaluationsCounter() const {
  return _gridboardThread->_gridboardsEvaluationsCounter;
}

qint64 Alerter::gridboardsUpdatesCounter() const {
  return _gridboardThread->_gridboardsUpdatesCounter;
}

QDateTime Alerter::windowCorrectedVisibilityDate(
    QString alertId, QDateTime uncorrectedVisibilityDate) {
  QDateTime visibilityDate = uncorrectedVisibilityDate;
  CronTrigger window = alertSettings(alertId).visibilityWindow();
  if (window.isValid()) {
    window.setLastTriggered(visibilityDate.addSecs(-1));
    visibilityDate = window.nextTriggering();
  }
  return visibilityDate;
}
