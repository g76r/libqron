/* Copyright 2014-2022 Hallowyn and others.
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
#include "schedulerconfig.h"
#include "eventsubscription.h"
#include "action/action.h"
#include "log/log.h"
#include "log/filelogger.h"
#include "configutils.h"
#include <QCryptographicHash>
#include <QMutex>
#include "tasksroot.h"

#define DEFAULT_MAXTOTALTASKINSTANCES 16
#define DEFAULT_MAXQUEUEDREQUESTS 128

static QString _uiHeaderNames[] {
  "Id", // 0
  "Last Load Time",
  "Is Active",
  "Actions"
};

static QSet<QString> excludedDescendantsForComments {
  "task", "taskgroup", "host", "cluster", "calendar", "alerts",
  "access-control", "onsuccess", "onfailure", "onfinish", "onstart",
  "onstderr", "onstdout", "onschedulerstart", "onconfigload", "onnotice"
};

static QStringList excludeOnfinishSubscriptions { "onfinish" };

namespace {

class RequestTaskActionLink {
public:
  Action _action;
  QString _eventType;
  QString _contextLabel;
  Task _contextTask;
  // TODO clarify "contextLabel" and "contextTask" names, contextTask is useless since only its id is used (and contextLabel=id)
  RequestTaskActionLink(Action action, QString eventType, QString contextLabel,
                        Task contextTask)
    : _action(action), _eventType(eventType), _contextLabel(contextLabel),
      _contextTask(contextTask) { }
};

} // unnamed namespace

class SchedulerConfigData : public SharedUiItemData{
public:
  QHash<QString,TaskGroup> _taskgroups;
  QHash<QString,TaskTemplate> _tasktemplates;
  QHash<QString,Task> _tasks;
  QHash<QString,Cluster> _clusters;
  QHash<QString,Host> _hosts;
  TasksRoot _tasksRoot;
  QList<EventSubscription> _onlog, _onnotice, _onschedulerstart, _onconfigload;
  qint32 _maxtotaltaskinstances, _maxqueuedrequests;
  QHash<QString,Calendar> _namedCalendars;
  AlerterConfig _alerterConfig;
  AccessControlConfig _accessControlConfig;
  QList<LogFile> _logfiles;
  QDateTime _lastLoadTime;
  QStringList _commentsList;
  mutable QMutex _mutex;
  mutable QString _id;
  PfNode _originalPfNode;
  SchedulerConfigData() : _maxtotaltaskinstances(0), _maxqueuedrequests(0) { }
  SchedulerConfigData(PfNode root, Scheduler *scheduler, bool applyLogConfig);
  SchedulerConfigData(const SchedulerConfigData &other)
    : SharedUiItemData(other),
      _taskgroups(other._taskgroups), _tasktemplates(other._tasktemplates),
      _tasks(other._tasks), _clusters(other._clusters), _hosts(other._hosts),
      _tasksRoot(other._tasksRoot), _onlog(other._onlog),
      _onnotice(other._onnotice), _onschedulerstart(other._onschedulerstart),
      _onconfigload(other._onconfigload),
      _maxtotaltaskinstances(other._maxtotaltaskinstances),
      _maxqueuedrequests(other._maxqueuedrequests),
      _namedCalendars(other._namedCalendars),
      _alerterConfig(other._alerterConfig),
      _accessControlConfig(other._accessControlConfig),
      _logfiles(other._logfiles),
      _lastLoadTime(other._lastLoadTime), _id(other._id) {
  }
  QVariant uiData(int section, int role) const;
  QVariant uiHeaderData(int section, int role) const;
  int uiSectionCount() const;
  QString id() const { return _id; }
  QString idQualifier() const { return "config"; }
  void applyLogConfig() const;
};

SchedulerConfig::SchedulerConfig() {
}

SchedulerConfig::SchedulerConfig(const SchedulerConfig &other)
  : SharedUiItem(other) {
}

SchedulerConfig::SchedulerConfig(PfNode root, Scheduler *scheduler,
                                 bool applyLogConfig)
  : SharedUiItem(new SchedulerConfigData(root, scheduler, applyLogConfig)) {
  recomputeId();
}

static inline void recordTaskActionLinks(
    PfNode parentNode, QStringList childNames,
    QList<RequestTaskActionLink> *requestTaskActionLinks,
    QString contextLabel, Task contextTask = Task()) {
  // TODO clarify "contextLabel" and "contextTask" names
  QStringList ignoredChildren;
  ignoredChildren << "cron" << "notice";
  for (auto childName: childNames)
    for (auto listnode: parentNode.childrenByName(childName)) {
      EventSubscription sub("", listnode, 0, ignoredChildren);
      foreach (Action a, sub.actions()) {
        if (a.actionType() == "requesttask"
            || a.actionType() == "plantask") {
          requestTaskActionLinks
              ->append(RequestTaskActionLink(a, childName, contextLabel,
                                             contextTask));
        }
      }
    }
}

SchedulerConfigData::SchedulerConfigData(
    PfNode root, Scheduler *scheduler, bool applyLogConfig)
  : _originalPfNode(root) {
  QList<RequestTaskActionLink> requestTaskActionLinks;
  QList<LogFile> logfiles;
  foreach (PfNode node, root.childrenByName("log")) {
    LogFile logfile(node);
    if (logfile.isNull()) {
      Log::warning() << "invalid log file declaration in configuration: "
                     << node.toPf();
    } else {
      Log::debug() << "adding logger " << node.toPf();
      logfiles.append(logfile);
    }
  }
  _logfiles = logfiles;
  if (applyLogConfig)
    this->applyLogConfig();
  _tasksRoot = TasksRoot(root, scheduler);
  _namedCalendars.clear();
  foreach (PfNode node, root.childrenByName("calendar")) {
    QString name = node.contentAsString();
    Calendar calendar(node);
    if (name.isEmpty())
      Log::error() << "ignoring anonymous calendar: " << node.toPf();
    else if (calendar.isNull())
      Log::error() << "ignoring empty calendar: " << node.toPf();
    else
      _namedCalendars.insert(name, calendar);
  }
  _hosts.clear();
  foreach (PfNode node, root.childrenByName("host")) {
    Host host(node, _tasksRoot.params());
    if (_hosts.contains(host.id())) {
      Log::error() << "ignoring duplicate host: " << host.id();
    } else {
      _hosts.insert(host.id(), host);
    }
  }
  // create default "localhost" host if it is not declared
  if (!_hosts.contains("localhost"))
    _hosts.insert("localhost", Host(PfNode("host", "localhost"),
                                    _tasksRoot.params()));
  _clusters.clear();
  foreach (PfNode node, root.childrenByName("cluster")) {
    Cluster cluster(node);
    foreach (PfNode child, node.childrenByName("hosts")) {
      foreach (const QString &hostId, child.contentAsStringList()) {
        Host host = _hosts.value(hostId);
        if (!host.isNull())
          cluster.appendHost(host);
        else
          Log::error() << "host '" << hostId
                       << "' not found, won't add it to cluster '"
                       << cluster.id() << "'";
      }
    }
    if (cluster.hosts().isEmpty())
      Log::warning() << "cluster '" << cluster.id() << "' has no member";
    if (_clusters.contains(cluster.id()))
      Log::error() << "ignoring duplicate cluster: " << cluster.id();
    else if (_hosts.contains(cluster.id()))
      Log::error() << "ignoring cluster which id conflicts with a host: "
                   << cluster.id();
    else
      _clusters.insert(cluster.id(), cluster);
  }
  _taskgroups.clear();
  QList<PfNode> taskGroupNodes = root.childrenByName("taskgroup");
  std::sort(taskGroupNodes.begin(), taskGroupNodes.end());
  for (auto node: taskGroupNodes) {
    QString id = ConfigUtils::sanitizeId(
          node.contentAsString(), ConfigUtils::FullyQualifiedId);
    if (_taskgroups.contains(id)) {
      Log::error() << "ignoring duplicate taskgroup: " << id;
      continue;
    }
    QString parentId = TaskGroup::parentGroupId(id);
    SharedUiItem parent = _taskgroups.value(parentId);
    if (parent.isNull())
      parent = _tasksRoot;
    TaskGroup taskGroup(node, parent, scheduler);
    if (taskGroup.isNull() || id.isEmpty()) {
      Log::error() << "ignoring invalid taskgroup: " << node.toPf();
      continue;
    }
    recordTaskActionLinks(
          node, { "onplan", "onstart", "onsuccess", "onfailure", "onfinish",
               "onstderr", "onstdout" },
          &requestTaskActionLinks, taskGroup.id()+".*");
    _taskgroups.insert(taskGroup.id(), taskGroup);
  }
  _tasktemplates.clear();
  for (auto node: root.childrenByName("tasktemplate")) {
    TaskTemplate tmpl(node, scheduler, _tasksRoot, _namedCalendars);
    if (tmpl.isNull()) { // cstr detected an error
      Log::error() << "ignoring invalid tasktemplate: " << node.toString();
      goto ignore_tasktemplate;
    }
    if (_tasktemplates.contains(tmpl.id())) {
      Log::error() << "ignoring duplicate tasktemplate " << tmpl.id();
      goto ignore_tasktemplate;
    }
    _tasktemplates.insert(tmpl.id(), tmpl);
ignore_tasktemplate:;
  }
  QHash<QString,Task> oldTasks = _tasks;
  _tasks.clear();
  foreach (PfNode node, root.childrenByName("task")) {
    QString taskGroupId = node.attribute("taskgroup");
    TaskGroup taskGroup = _taskgroups.value(taskGroupId);
    Task task(node, scheduler, taskGroup, _namedCalendars, _tasktemplates);
    if (taskGroupId.isEmpty() || taskGroup.isNull()) {
      Log::error() << "ignoring task with invalid taskgroup: " << node.toPf();
      goto ignore_task;
    }
    if (task.isNull()) { // Task cstr detected an error
      Log::error() << "ignoring invalid task: " << node.toString();
      goto ignore_task;
    }
    if (_tasks.contains(task.id())) {
      Log::error() << "ignoring duplicate task " << task.id();
      goto ignore_task;
    }
    _tasks.insert(task.id(), task);
    recordTaskActionLinks(
          node, { "onplan", "onstart", "onsuccess", "onfailure", "onfinish",
               "onstderr", "onstdout" },
          &requestTaskActionLinks, task.id(), task);
    for (auto tmpl: task.appliedTemplates()) {
      recordTaskActionLinks(
            tmpl.originalPfNode(),
            { "onplan", "onstart", "onsuccess", "onfailure", "onfinish",
           "onstderr", "onstdout" },
            &requestTaskActionLinks, task.id(), task);
    }
ignore_task:;
  }
  int maxtotaltaskinstances = 0;
  foreach (PfNode node, root.childrenByName("maxtotaltaskinstances")) {
    int n = node.contentAsString().toInt(0, 0);
    if (n > 0) {
      if (maxtotaltaskinstances > 0)
        Log::error() << "overriding maxtotaltaskinstances "
                     << maxtotaltaskinstances << " with " << n;
      maxtotaltaskinstances = n;
    } else
      Log::error() << "ignoring maxtotaltaskinstances with incorrect "
                      "value: " << node.toPf();
  }
  if (maxtotaltaskinstances <= 0) {
    maxtotaltaskinstances = DEFAULT_MAXTOTALTASKINSTANCES;
    Log::debug() << "configured " << maxtotaltaskinstances
                 << " task executors (default maxtotaltaskinstances value)";
  }
  _maxtotaltaskinstances = maxtotaltaskinstances;
  int maxqueuedrequests = 0;
  foreach (PfNode node, root.childrenByName("maxqueuedrequests")) {
    int n = node.contentAsString().toInt(0, 0);
    if (n > 0) {
      if (maxqueuedrequests > 0) {
        Log::warning() << "overriding maxqueuedrequests "
                       << maxqueuedrequests << " with " << n;
      }
      maxqueuedrequests = n;
    } else {
      Log::warning() << "ignoring maxqueuedrequests with incorrect "
                        "value: " << node.toPf();
    }
  }
  if (maxqueuedrequests <= 0)
    maxqueuedrequests = DEFAULT_MAXQUEUEDREQUESTS;
  _maxqueuedrequests = maxqueuedrequests;
  QList<PfNode> alerts = root.childrenByName("alerts");
  if (alerts.size() > 1)
    Log::error() << "multiple alerts configuration (ignoring all but last one)";
  if (alerts.size())
    _alerterConfig = AlerterConfig(alerts.last());
  else // build an empty but not null AlerterConfig
    _alerterConfig = AlerterConfig(PfNode(QStringLiteral("alerts")));
  _onlog.clear();
  ConfigUtils::loadEventSubscription(root, "onlog", "*", &_onlog, scheduler);
  _onnotice.clear();
  ConfigUtils::loadEventSubscription(root, "onnotice", "*", &_onnotice,
                                     scheduler);
  _onschedulerstart.clear();
  ConfigUtils::loadEventSubscription(root, "onschedulerstart", "*",
                                     &_onschedulerstart, scheduler);
  _onconfigload.clear();
  ConfigUtils::loadEventSubscription(root, "onconfigload", "*",
                                     &_onconfigload, scheduler);
  recordTaskActionLinks(
        root, { "onstart", "onsuccess", "onfailure", "onfinish", "onlog",
                "onnotice", "onschedulerstart", "onconfigload" },
        &requestTaskActionLinks, "*");
  // LATER onschedulershutdown
  foreach (const RequestTaskActionLink &link, requestTaskActionLinks) {
    QString targetName = link._action.targetName();
    QString taskId = link._contextTask.id();
    if (!link._contextTask.isNull() && !targetName.contains('.')) { // id to group
      targetName = taskId.left(taskId.lastIndexOf('.')+1)+targetName;
    }
    if (_tasks.contains(targetName)) {
      QString context(link._contextLabel);
      if (!link._contextTask.isNull())
        context = taskId;
      if (context.isEmpty())
        context = "*";
      _tasks[targetName].appendOtherTriggers("*"+link._eventType+"("+context+")");
      //Log::fatal() << "*** " << _tasks[targetName].otherTriggers() << " *** " << link._contextLabel << " *** " << taskId;
    } else {
      Log::debug() << "cannot translate event " << link._eventType
                   << " for task '" << targetName << "'";
    }
  }
  QList<PfNode> accessControl = root.childrenByName("access-control");
  if (accessControl.size() > 0) {
    _accessControlConfig = AccessControlConfig(accessControl.first());
    if (accessControl.size() > 1) {
      Log::error() << "ignoring multiple 'access-control' in configuration";
    }
  }
  _lastLoadTime = QDateTime::currentDateTime();
  ConfigUtils::loadComments(root, &_commentsList,
                            excludedDescendantsForComments);
}

SchedulerConfig::~SchedulerConfig() {
}

bool SchedulerConfig::isNull() const {
  return !data();
}

TasksRoot SchedulerConfig::tasksRoot() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasksRoot : TasksRoot();
}

QHash<QString,TaskGroup> SchedulerConfig::taskgroups() const {
  const SchedulerConfigData *d = data();
  return d ? d->_taskgroups : QHash<QString,TaskGroup>();
}

QHash<QString,TaskTemplate> SchedulerConfig::tasktemplates() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasktemplates : QHash<QString,TaskTemplate>();
}

QHash<QString,Task> SchedulerConfig::tasks() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasks : QHash<QString,Task>();
}

QHash<QString,Cluster> SchedulerConfig::clusters() const {
  const SchedulerConfigData *d = data();
  return d ? d->_clusters : QHash<QString,Cluster>();
}

QHash<QString,Host> SchedulerConfig::hosts() const {
  const SchedulerConfigData *d = data();
  return d ? d->_hosts : QHash<QString,Host>();
}

QHash<QString,Calendar> SchedulerConfig::namedCalendars() const {
  const SchedulerConfigData *d = data();
  return d ? d->_namedCalendars : QHash<QString,Calendar>();
}

QList<EventSubscription> SchedulerConfig::onstart() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasksRoot.onstart() : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onsuccess() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasksRoot.onsuccess() : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onfailure() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasksRoot.onfailure() : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onlog() const {
  const SchedulerConfigData *d = data();
  return d ? d->_onlog : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onnotice() const {
  const SchedulerConfigData *d = data();
  return d ? d->_onnotice : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onschedulerstart() const {
  const SchedulerConfigData *d = data();
  return d ? d->_onschedulerstart : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::onconfigload() const {
  const SchedulerConfigData *d = data();
  return d ? d->_onconfigload : QList<EventSubscription>();
}

QList<EventSubscription> SchedulerConfig::allEventsSubscriptions() const {
  const SchedulerConfigData *d = data();
  return d ? d->_tasksRoot.allEventSubscriptions() + d->_onlog
             + d->_onnotice + d->_onschedulerstart + d->_onconfigload
           : QList<EventSubscription>();
}

qint32 SchedulerConfig::maxtotaltaskinstances() const {
  const SchedulerConfigData *d = data();
  return d ? d->_maxtotaltaskinstances : 0;
}

qint32 SchedulerConfig::maxqueuedrequests() const {
  const SchedulerConfigData *d = data();
  return d ? d->_maxqueuedrequests : 0;
}

AlerterConfig SchedulerConfig::alerterConfig() const {
  const SchedulerConfigData *d = data();
  return d ? d->_alerterConfig : AlerterConfig();
}

AccessControlConfig SchedulerConfig::accessControlConfig() const {
  const SchedulerConfigData *d = data();
  return d ? d->_accessControlConfig : AccessControlConfig();
}

QList<LogFile> SchedulerConfig::logfiles() const {
  const SchedulerConfigData *d = data();
  return d ? d->_logfiles : QList<LogFile>();
}

void SchedulerConfig::changeItem(
    SharedUiItem newItem, SharedUiItem oldItem, QString idQualifier) {
  SchedulerConfigData *d = data();
  if (!d)
    setData(d = new SchedulerConfigData());
  if (idQualifier == "task") {
    Task &actualNewItem = reinterpret_cast<Task&>(newItem);
    d->_tasks.remove(oldItem.id());
    if (!newItem.isNull())
      d->_tasks.insert(newItem.id(), actualNewItem);
    recomputeId();
  } else if (idQualifier == "taskgroup") {
      TaskGroup &actualNewItem = reinterpret_cast<TaskGroup&>(newItem);
      d->_taskgroups.remove(oldItem.id());
      if (!newItem.isNull())
        d->_taskgroups.insert(newItem.id(), actualNewItem);
      recomputeId();
  } else if (idQualifier == "cluster") {
    Cluster &actualNewItem = reinterpret_cast<Cluster&>(newItem);
    d->_clusters.remove(oldItem.id());
    if (!newItem.isNull())
      d->_clusters.insert(newItem.id(), actualNewItem);
    recomputeId();
  } else if (idQualifier == "host") {
    Host &actualNewItem = reinterpret_cast<Host&>(newItem);
    d->_hosts.remove(oldItem.id());
    if (!newItem.isNull())
      d->_hosts.insert(newItem.id(), actualNewItem);
    recomputeId();
  } else {
    qDebug() << "unsupported item type in SchedulerConfig::changeItem:"
             << idQualifier;
  }
}

/*void SchedulerConfig::changeParams(
    ParamSet newParams, ParamSet oldParams, QString setId) {
  Q_UNUSED(oldParams)
  SchedulerConfigData *d = data();
  if (!d)
    setData(d = new SchedulerConfigData());
  if (setId == QStringLiteral("globalparams")) {
    d->_params = newParams;
  } else if (setId == QStringLiteral("globalvars")) {
    d->_vars = newParams;
  } else {
    qWarning() << "SchedulerConfig::changeParams() called with "
                  "unknown paramsetid:" << setId;
  }
  recomputeId();
}*/

QString SchedulerConfig::recomputeId() const {
  const SchedulerConfigData *d = data();
  if (!d)
    return QString();
  QMutexLocker locker(&d->_mutex);
  QCryptographicHash hash(QCryptographicHash::Sha1);
  QByteArray data;
  QBuffer buf(&data);
  buf.open(QIODevice::ReadWrite);
  buf.write(toPfNode()
            .toPf(PfOptions().setShouldIndent()
                  .setShouldWriteContentBeforeSubnodes()
                  .setShouldIgnoreComment(false)));
  buf.seek(0);
  hash.addData(&buf);
  d->_id = hash.result().toHex();
  return d->_id;
}

void SchedulerConfig::copyLiveAttributesFromOldTasks(
    QHash<QString,Task> oldTasks) {
  SchedulerConfigData *d = data();
  if (!d)
    return;
  foreach (const Task &oldTask, oldTasks) {
    Task task = d->_tasks.value(oldTask.id());
    if (task.isNull())
      continue;
    task.copyLiveAttributesFromOldTask(oldTask);
    d->_tasks.insert(task.id(), task);
  }
}

PfNode SchedulerConfig::originalPfNode() const {
  const SchedulerConfigData *d = data();
  if (!d)
    return PfNode();
  return d->_originalPfNode;
}

PfNode SchedulerConfig::toPfNode() const {
  const SchedulerConfigData *d = data();
  if (!d)
    return PfNode();
  PfNode node("config");
  ConfigUtils::writeComments(&node, d->_commentsList);
  ConfigUtils::writeParamSet(&node, d->_tasksRoot.params(), QStringLiteral("param"));
  ConfigUtils::writeParamSet(&node, d->_tasksRoot.vars(), QStringLiteral("var"));
  ConfigUtils::writeParamSet(&node, d->_tasksRoot.instanceparams(),
                             QStringLiteral("instanceparam"));
  ConfigUtils::writeEventSubscriptions(&node, d->_tasksRoot.onstart());
  ConfigUtils::writeEventSubscriptions(&node, d->_tasksRoot.onsuccess());
  ConfigUtils::writeEventSubscriptions(&node, d->_tasksRoot.onfailure(),
                                       excludeOnfinishSubscriptions);
  ConfigUtils::writeEventSubscriptions(&node, d->_onlog);
  ConfigUtils::writeEventSubscriptions(&node, d->_onnotice);
  ConfigUtils::writeEventSubscriptions(&node, d->_onschedulerstart);
  ConfigUtils::writeEventSubscriptions(&node, d->_onconfigload);
  QList<TaskGroup> taskGroups = d->_taskgroups.values();
  std::sort(taskGroups.begin(), taskGroups.end());
  foreach(const TaskGroup &taskGroup, taskGroups)
    node.appendChild(taskGroup.toPfNode());
  QList<Task> tasks = d->_tasks.values();
  std::sort(tasks.begin(), tasks.end());
  foreach(const Task &task, tasks)
    node.appendChild(task.toPfNode());
  QList<Host> hosts = d->_hosts.values();
  std::sort(hosts.begin(), hosts.end());
  foreach (const Host &host, hosts)
    node.appendChild(host.toPfNode());
  QList<Cluster> clusters = d->_clusters.values();
  std::sort(clusters.begin(), clusters.end());
  foreach (const Cluster &cluster, clusters)
    node.appendChild(cluster.toPfNode());
  if (d->_maxtotaltaskinstances != DEFAULT_MAXTOTALTASKINSTANCES)
    node.setAttribute(QStringLiteral("maxtotaltaskinstances"),
                      QString::number(d->_maxtotaltaskinstances));
  if (d->_maxqueuedrequests != DEFAULT_MAXQUEUEDREQUESTS)
    node.setAttribute(QStringLiteral("maxqueuedrequests"),
                      QString::number(d->_maxqueuedrequests));
  // LATER use this when Calendar will be ported to SharedUiItem
  //QList<Calendar> namedCalendars = d->_namedCalendars.values();
  //qSort(namedCalendars);
  //foreach (const Calendar &calendar, namedCalendars)
  //  node.appendChild(calendar.toPfNode());
  QStringList calendarNames = d->_namedCalendars.keys();
  std::sort(calendarNames.begin(), calendarNames.end());
  foreach (const QString &calendarName, calendarNames)
    node.appendChild(d->_namedCalendars.value(calendarName).toPfNode());
  node.appendChild(d->_alerterConfig.toPfNode());
  if (!d->_accessControlConfig.isEmpty())
    node.appendChild(d->_accessControlConfig.toPfNode());
  foreach (const LogFile &logfile, d->_logfiles)
    node.appendChild(logfile.toPfNode());
  return node;
}

SchedulerConfigData *SchedulerConfig::data() {
  return detachedData<SchedulerConfigData>();
}

QVariant SchedulerConfigData::uiHeaderData(int section, int role) const {
  return role == Qt::DisplayRole && section >= 0
      && (unsigned)section < sizeof _uiHeaderNames
      ? _uiHeaderNames[section] : QVariant();
}

int SchedulerConfigData::uiSectionCount() const {
  return sizeof _uiHeaderNames / sizeof *_uiHeaderNames;
}

QVariant SchedulerConfigData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _lastLoadTime.toString("yyyy-MM-dd hh:mm:ss,zzz");
    }
  }
  return QVariant();
}

void SchedulerConfigData::applyLogConfig() const {
  QList<Logger*> loggers;
  foreach (LogFile logfile, _logfiles) {
    loggers.append(new FileLogger(
                     logfile.pathPattern(), logfile.minimumSeverity(), 60,
                     logfile.buffered()));
  }
  // LATER make console severity log level a parameter
  Log::replaceLoggersPlusConsole(Log::Fatal, loggers, true);
}

void SchedulerConfig::applyLogConfig() const {
  const SchedulerConfigData *d = data();
  if (d)
    d->applyLogConfig();
}
