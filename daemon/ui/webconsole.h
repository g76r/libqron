/* Copyright 2012-2013 Hallowyn and others.
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
#ifndef WEBCONSOLE_H
#define WEBCONSOLE_H

#include "httpd/httphandler.h"
#include "httpd/templatinghttphandler.h"
#include "textview/htmltableview.h"
#include "textview/csvtableview.h"
#include "sched/scheduler.h"
#include "resourcesallocationmodel.h"
#include "resourcesconsumptionmodel.h"
#include "hostslistmodel.h"
#include "clusterslistmodel.h"
#include "util/paramsetmodel.h"
#include "raisedalertsmodel.h"
#include "lastoccuredtexteventsmodel.h"
#include "textview/clockview.h"
#include "alertrulesmodel.h"
#include "log/memorylogger.h"
#include "taskrequestsmodel.h"
#include "tasksmodel.h"
#include "schedulereventsmodel.h"
#include "flagssetmodel.h"
#include "taskgroupsmodel.h"
#include "lastemitedalertsmodel.h"
#include "alertchannelsmodel.h"
#include "auth/inmemoryrulesauthorizer.h"
#include "auth/usersdatabase.h"
#include "httpd/graphvizimagehttphandler.h"

class QThread;

class WebConsole : public HttpHandler {
  friend class WebConsoleParamsProvider;
  Q_OBJECT
  Q_DISABLE_COPY(WebConsole)
  QThread *_thread;
  Scheduler *_scheduler;
  HostsListModel *_hostsListModel;
  ClustersListModel *_clustersListModel;
  ResourcesAllocationModel *_freeResourcesModel, *_resourcesLwmModel;
  ResourcesConsumptionModel *_resourcesConsumptionModel;
  ParamSetModel *_globalParamsModel, *_globalSetenvModel, *_globalUnsetenvModel,
  *_alertParamsModel;
  RaisedAlertsModel *_raisedAlertsModel;
  LastEmitedAlertsModel *_lastEmitedAlertsModel;
  LastOccuredTextEventsModel *_lastPostedNoticesModel, *_lastFlagsChangesModel;
  AlertRulesModel *_alertRulesModel;
  TaskRequestsModel *_taskRequestsHistoryModel, *_unfinishedTaskRequestModel;
  TasksModel *_tasksModel;
  SchedulerEventsModel *_schedulerEventsModel;
  FlagsSetModel *_flagsSetModel;
  TaskGroupsModel *_taskGroupsModel;
  AlertChannelsModel *_alertChannelsModel;
  HtmlTableView *_htmlHostsListView, *_htmlClustersListView,
  *_htmlFreeResourcesView, *_htmlResourcesLwmView,
  *_htmlResourcesConsumptionView, *_htmlGlobalParamsView,
  *_htmlGlobalSetenvView, *_htmlGlobalUnsetenvView, *_htmlAlertParamsView,
  *_htmlRaisedAlertsView, *_htmlRaisedAlertsView10, *_htmlLastEmitedAlertsView,
  *_htmlLastEmitedAlertsView10, *_htmlAlertRulesView, *_htmlWarningLogView,
  *_htmlWarningLogView10, *_htmlInfoLogView,
  *_htmlTaskRequestsView, *_htmlTaskRequestsView20,
  *_htmlTasksScheduleView, *_htmlTasksConfigView, *_htmlTasksParamsView,
  *_htmlTasksListView,
  *_htmlTasksEventsView, *_htmlSchedulerEventsView,
  *_htmlLastPostedNoticesView20, *_htmlLastFlagsChangesView20,
  *_htmlFlagsSetView20, *_htmlTaskGroupsView, *_htmlTaskGroupsEventsView,
  *_htmlAlertChannelsView, *_htmlTasksResourcesView, *_htmlTasksAlertsView;
  ClockView *_clockView;
  CsvTableView *_csvHostsListView,
  *_csvClustersListView, *_csvFreeResourcesView, *_csvResourcesLwmView,
  *_csvResourcesConsumptionView, *_csvGlobalParamsView,
  *_csvGlobalSetenvView, *_csvGlobalUnsetenvView,
  *_csvAlertParamsView, *_csvRaisedAlertsView, *_csvLastEmitedAlertsView,
  *_csvAlertRulesView, *_csvLogView, *_csvTaskRequestsView, *_csvTasksView,
  *_csvSchedulerEventsView, *_csvLastPostedNoticesView,
  *_csvLastFlagsChangesView, *_csvFlagsSetView, *_csvTaskGroupsView;
  GraphvizImageHttpHandler *_tasksDeploymentDiagram, *_tasksTriggerDiagram;
  TemplatingHttpHandler *_wuiHandler;
  MemoryLogger *_memoryInfoLogger, *_memoryWarningLogger;
  QString _title, _navtitle, _cssoverload, _customaction_taskdetail;
  InMemoryRulesAuthorizer *_authorizer;
  UsersDatabase *_usersDatabase;
  bool _ownUsersDatabase, _accessControlEnabled;
  QHash<QString,TaskGroup> _tasksGroups;
  QHash<QString,Task> _tasks;
  QHash<QString,Cluster> _clusters;
  QHash<QString,Host> _hosts;
  QMultiHash<QString,Event> _schedulerEvents;

public:
  WebConsole();
  ~WebConsole();
  bool acceptRequest(HttpRequest req);
  bool handleRequest(HttpRequest req, HttpResponse res,
                     HttpRequestContext ctxt);
  void setScheduler(Scheduler *scheduler);
  void setUsersDatabase(UsersDatabase *usersDatabase, bool takeOwnership);

public slots:
  void enableAccessControl(bool enabled);

signals:
  void flagChange(QString change, int type);
  void alertEmited(QString alert, int type);

private slots:
  void flagSet(QString flag);
  void flagCleared(QString flag);
  void alertEmited(QString alert);
  void alertCancellationEmited(QString alert);
  void globalParamsChanged(ParamSet globalParams);
  void tasksConfigurationReset(QHash<QString,TaskGroup> tasksGroups,
                               QHash<QString,Task> tasks);
  void targetsConfigurationReset(QHash<QString,Cluster> clusters,
                                 QHash<QString,Host> hosts);
  void schedulerEventsConfigurationReset(
      QList<Event> onstart, QList<Event> onsuccess, QList<Event> onfailure,
      QList<Event> onlog, QList<Event> onnotice, QList<Event> onschedulerstart,
      QList<Event> onconfigload);
  void configReloaded();

private:
  static void copyFilteredFiles(QStringList paths, QIODevice *output,
                               QString pattern, bool useRegexp);
  static void copyFilteredFile(QString path, QIODevice *output,
                               QString pattern, bool useRegexp) {
    QStringList paths;
    paths.append(path);
    copyFilteredFiles(paths, output, pattern, useRegexp); }
  void recomputeDiagrams();
};

#endif // WEBCONSOLE_H
