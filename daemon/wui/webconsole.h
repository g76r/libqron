/* Copyright 2012-2014 Hallowyn and others.
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
#include "ui/hostsresourcesavailabilitymodel.h"
#include "ui/resourcesconsumptionmodel.h"
#include "ui/hostslistmodel.h"
#include "ui/clusterslistmodel.h"
#include "util/paramsetmodel.h"
#include "ui/raisedalertsmodel.h"
#include "ui/lastoccuredtexteventsmodel.h"
#include "textview/clockview.h"
#include "ui/alertrulesmodel.h"
#include "log/memorylogger.h"
#include "ui/taskinstancesmodel.h"
#include "ui/tasksmodel.h"
#include "ui/schedulereventsmodel.h"
#include "ui/taskgroupsmodel.h"
#include "ui/alertchannelsmodel.h"
#include "auth/inmemoryrulesauthorizer.h"
#include "auth/usersdatabase.h"
#include "httpd/graphvizimagehttphandler.h"
#include "ui/logfilesmodel.h"
#include "ui/calendarsmodel.h"
#include <QSortFilterProxyModel>
#include "ui/stepsmodel.h"
#include "ui/htmlstepitemdelegate.h"

class QThread;

/** Central class for qron web console.
 * Mainly sets HTML templating and views up and dispatches request between views
 * and event handling. */
class WebConsole : public HttpHandler {
  friend class WebConsoleParamsProvider;
  Q_OBJECT
  Q_DISABLE_COPY(WebConsole)
  QThread *_thread;
  Scheduler *_scheduler;
  HostsListModel *_hostsListModel;
  ClustersListModel *_clustersListModel;
  HostsResourcesAvailabilityModel *_freeResourcesModel, *_resourcesLwmModel;
  ResourcesConsumptionModel *_resourcesConsumptionModel;
  ParamSetModel *_globalParamsModel, *_globalSetenvModel, *_globalUnsetenvModel,
  *_alertParamsModel;
  RaisedAlertsModel *_raisedAlertsModel;
  LastOccuredTextEventsModel *_lastEmitedAlertsModel, *_lastPostedNoticesModel;
  AlertRulesModel *_alertRulesModel;
  TaskInstancesModel *_taskInstancesHistoryModel, *_unfinishedTaskInstancetModel;
  TasksModel *_tasksModel;
  QSortFilterProxyModel *_mainTasksModel, *_subtasksModel;
  SchedulerEventsModel *_schedulerEventsModel;
  TaskGroupsModel *_taskGroupsModel;
  AlertChannelsModel *_alertChannelsModel;
  LogFilesModel *_logConfigurationModel;
  CalendarsModel *_calendarsModel;
  StepsModel *_stepsModel;
  HtmlTableView *_htmlHostsListView, *_htmlClustersListView,
  *_htmlFreeResourcesView, *_htmlResourcesLwmView,
  *_htmlResourcesConsumptionView, *_htmlGlobalParamsView,
  *_htmlGlobalSetenvView, *_htmlGlobalUnsetenvView, *_htmlAlertParamsView,
  *_htmlRaisedAlertsView, *_htmlRaisedAlertsView10, *_htmlLastEmitedAlertsView,
  *_htmlLastEmitedAlertsView10, *_htmlAlertRulesView, *_htmlWarningLogView,
  *_htmlWarningLogView10, *_htmlInfoLogView,
  *_htmlTaskInstancesView, *_htmlTaskInstancesView20,
  *_htmlTasksScheduleView, *_htmlTasksConfigView, *_htmlTasksParamsView,
  *_htmlTasksListView,
  *_htmlTasksEventsView, *_htmlSchedulerEventsView,
  *_htmlLastPostedNoticesView20,
  *_htmlTaskGroupsView, *_htmlTaskGroupsEventsView,
  *_htmlAlertChannelsView, *_htmlTasksResourcesView, *_htmlTasksAlertsView,
  *_htmlLogFilesView, *_htmlCalendarsView, *_htmlStepsView;
  ClockView *_clockView;
  CsvTableView *_csvHostsListView,
  *_csvClustersListView, *_csvFreeResourcesView, *_csvResourcesLwmView,
  *_csvResourcesConsumptionView, *_csvGlobalParamsView,
  *_csvGlobalSetenvView, *_csvGlobalUnsetenvView,
  *_csvAlertParamsView, *_csvRaisedAlertsView, *_csvLastEmitedAlertsView,
  *_csvAlertRulesView, *_csvLogView, *_csvTaskInstancesView, *_csvTasksView,
  *_csvSchedulerEventsView, *_csvLastPostedNoticesView,
  *_csvTaskGroupsView, *_csvLogFilesView, *_csvCalendarsView, *_csvStepsView;
  GraphvizImageHttpHandler *_tasksDeploymentDiagram, *_tasksTriggerDiagram;
  TemplatingHttpHandler *_wuiHandler;
  MemoryLogger *_memoryInfoLogger, *_memoryWarningLogger;
  QString _title, _navtitle, _titlehref, _cssoverload, _customaction_taskdetail;
  InMemoryRulesAuthorizer *_authorizer;
  UsersDatabase *_usersDatabase;
  bool _ownUsersDatabase, _accessControlEnabled, _loggersAdded;

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
  void alertEmited(QString alert, int type);

private slots:
  void alertEmited(QString alert);
  void alertCancellationEmited(QString alert);
  void globalParamsChanged(ParamSet globalParams);
  void configChanged(SchedulerConfig config);

private:
  static void copyFilteredFiles(QStringList paths, QIODevice *output,
                               QString pattern, bool useRegexp);
  static void copyFilteredFile(QString path, QIODevice *output,
                               QString pattern, bool useRegexp) {
    QStringList paths;
    paths.append(path);
    copyFilteredFiles(paths, output, pattern, useRegexp); }
};

#endif // WEBCONSOLE_H