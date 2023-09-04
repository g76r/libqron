/* Copyright 2015-2023 Hallowyn and others.
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
#ifndef QRONCONFIGDOCUMENTMANAGER_H
#define QRONCONFIGDOCUMENTMANAGER_H

#include "config/schedulerconfig.h"
#include "eventsubscription.h"
#include "modelview/shareduiitemdocumentmanager.h"
#include <QMutexLocker>

/** Document manager for scheduler config
 * @see SharedUiItemDocumentManager
 * @see SchedulerConfig
 */
class LIBQRONSHARED_EXPORT QronConfigDocumentManager
    : public SharedUiItemDocumentManager {
  Q_OBJECT
  Q_DISABLE_COPY(QronConfigDocumentManager)
  SchedulerConfig _config;

public:
  explicit QronConfigDocumentManager(QObject *parent = 0);
  SchedulerConfig config() const { return _config; }
  /** If locker != 0, unlock it as soon as _config is set (i.e. as soon as
   * config() is thread-safe again */
  void setConfig(SchedulerConfig newConfig, QMutexLocker<QMutex> *locker = 0);
  using SharedUiItemDocumentManager::itemById;
  SharedUiItem itemById(
      const Utf8String &qualifier, const Utf8String &id) const override;
  using SharedUiItemDocumentManager::itemsByQualifier;
  SharedUiItemList<SharedUiItem> itemsByQualifier(
      const Utf8String &qualifier) const override;
  QMap<Utf8String,Calendar> namedCalendars() const {
    return _config.namedCalendars(); }
  QMap<Utf8String,PfNode> externalParams() const {
    return _config.externalParams(); }
  int tasksCount() const { return _config.tasks().count(); }
  int taskGroupsCount() const { return _config.taskgroups().count(); }
  int maxtotaltaskinstances() const { return _config.maxtotaltaskinstances(); }
  int maxqueuedrequests() const { return _config.maxqueuedrequests(); }
  Calendar calendarByName(QByteArray name) const {
    return _config.namedCalendars().value(name); }
  ParamSet globalParams() const { return _config.params(); }
  ParamSet globalVars() const { return _config.vars(); }
  /** This method is threadsafe */
  bool taskExists(QByteArray taskId) {
    return _config.tasks().contains(taskId); }
  /** This method is threadsafe */
  Task task(QByteArray taskId) { return _config.tasks().value(taskId); }
  void changeParams(ParamSet newParams, ParamSet oldParams, QByteArray setId);

signals:
  void logConfigurationChanged(SharedUiItemList<> logfiles);
  void paramsChanged(ParamSet newParams, ParamSet oldParams, QByteArray setId);
  void accessControlConfigurationChanged(bool enabled);
  void globalEventSubscriptionsChanged(
      QList<EventSubscription> onstart, QList<EventSubscription> onsuccess,
      QList<EventSubscription> onfailure, QList<EventSubscription> onlog,
      QList<EventSubscription> onnotice,
      QList<EventSubscription> onschedulerstart,
      QList<EventSubscription> onconfigload);

protected:
  bool prepareChangeItem(
      SharedUiItemDocumentTransaction *transaction,
      const SharedUiItem &new_item, const SharedUiItem &old_item,
      const Utf8String &qualifier, QString *errorString) override;
  void commitChangeItem(
      const SharedUiItem &new_item, const SharedUiItem &old_item,
      const Utf8String &qualifier) override;

private:
  template<class T>
  void inline emitSignalForItemTypeChanges(
      const QMap<Utf8String,T> &newItems, const QMap<Utf8String,T> &oldItems,
      const Utf8String &qualifier);
  /*template<>
  void inline emitSignalForItemTypeChanges(
      QMap<QByteArray,T> newItems, QMap<QByteArray,T> oldItems,
      QByteArray qualifier);
  */
};

#endif // QRONCONFIGDOCUMENTMANAGER_H
