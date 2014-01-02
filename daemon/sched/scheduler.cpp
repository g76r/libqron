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
#include "scheduler.h"
#include <QtDebug>
#include <QCoreApplication>
#include <QEvent>
#include "pf/pfparser.h"
#include "pf/pfdomhandler.h"
#include "config/host.h"
#include "config/cluster.h"
#include "util/timerwitharguments.h"
#include <QMetaObject>
#include "log/log.h"
#include "log/filelogger.h"
#include <QFile>
#include <stdio.h>
#include "action/requesttaskaction.h"
#include <QThread>
#include "config/configutils.h"
#include "config/requestformfield.h"
#include "config/step.h"
#include "trigger/crontrigger.h"
#include "trigger/noticetrigger.h"

#define REEVALUATE_QUEUED_REQUEST_EVENT (QEvent::Type(QEvent::User+1))
#define DEFAULT_MAXQUEUEDREQUESTS 128

class Scheduler::RequestTaskActionLink {
public:
  Action _action;
  QString _eventType;
  QString _contextLabel;
  Task _contextTask;
  // TODO clarify "contextLabel" and "contextTask" names
  RequestTaskActionLink(Action action, QString eventType, QString contextLabel,
                        Task contextTask)
    : _action(action), _eventType(eventType), _contextLabel(contextLabel),
      _contextTask(contextTask) { }
};

Scheduler::Scheduler() : QObject(0), _thread(new QThread()),
  _configMutex(QMutex::Recursive),
  _alerter(new Alerter), _authenticator(new InMemoryAuthenticator(this)),
  _usersDatabase(new InMemoryUsersDatabase(this)),
  _firstConfigurationLoad(true),
  _maxtotaltaskinstances(0),
  _startdate(QDateTime::currentDateTime().toMSecsSinceEpoch()),
  _configdate(LLONG_MIN), _execCount(0), _accessControlFilesWatcher(0) {
  _thread->setObjectName("SchedulerThread");
  connect(this, SIGNAL(destroyed(QObject*)), _thread, SLOT(quit()));
  connect(_thread, SIGNAL(finished()), _thread, SLOT(deleteLater()));
  _thread->start();
  qRegisterMetaType<Task>("Task");
  qRegisterMetaType<TaskInstance>("TaskInstance");
  qRegisterMetaType<QList<TaskInstance> >("QList<TaskInstance>");
  qRegisterMetaType<Host>("Host");
  qRegisterMetaType<LogFile>("LogFile");
  qRegisterMetaType<QPointer<Executor> >("QPointer<Executor>");
  qRegisterMetaType<QList<EventSubscription> >("QList<EventSubscription>");
  qRegisterMetaType<QHash<QString,Task> >("QHash<QString,Task>");
  qRegisterMetaType<QHash<QString,TaskGroup> >("QHash<QString,TaskGroup>");
  qRegisterMetaType<QHash<QString,QHash<QString,qint64> > >("QHash<QString,QHash<QString,qint64> >");
  qRegisterMetaType<QHash<QString,Cluster> >("QHash<QString,Cluster>");
  qRegisterMetaType<QHash<QString,Host> >("QHash<QString,Host>");
  qRegisterMetaType<QHash<QString,qint64> >("QHash<QString,qint64>");
  qRegisterMetaType<QList<LogFile> >("QList<LogFile>");
  qRegisterMetaType<QHash<QString,Calendar> >("QHash<QString,Calendar>");
  QTimer *timer = new QTimer(this);
  connect(timer, SIGNAL(timeout()), this, SLOT(periodicChecks()));
  timer->start(60000);
  moveToThread(_thread);
}

Scheduler::~Scheduler() {
  Log::clearLoggers();
  _alerter->deleteLater();
}

bool Scheduler::reloadConfiguration(QIODevice *source) {
  if (!source->isOpen())
    if (!source->open(QIODevice::ReadOnly)) {
      QString errorString = source->errorString();
      Log::error() << "cannot read configuration: " << errorString;
      return false;
    }
  PfDomHandler pdh;
  PfParser pp(&pdh);
  pp.parse(source);
  if (pdh.errorOccured()) {
    QString errorString = pdh.errorString()+" at line "
        +QString::number(pdh.errorLine())
        +" column "+QString::number(pdh.errorColumn());
    Log::error() << "empty or invalid configuration: " << errorString;
    return false;
  }
  QList<PfNode> roots = pdh.roots();
  if (roots.size() == 0) {
    Log::error() << "configuration lacking root node";
  } else if (roots.size() == 1) {
    PfNode &root(roots.first());
    if (root.name() == "qrontab") {
      bool ok = false;
      if (QThread::currentThread() == thread())
        ok = reloadConfiguration(root);
      else
        QMetaObject::invokeMethod(this, "reloadConfiguration",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ok),
                                  Q_ARG(PfNode, root));
      return ok;
    } else {
      Log::error() << "configuration root node is not \"qrontab\"";
    }
  } else {
    Log::error() << "configuration with more than one root node";
  }
  return false;
}

bool Scheduler::reloadConfiguration(PfNode root) {
  QMutexLocker ml(&_configMutex);
  // LATER make sanity checks before loading the config and return false, to
  // have a chance to avoid loading a totally stupid configuration
  _resources.clear();
  _unsetenv.clear();
  _globalParams.clear();
  _setenv.clear();
  _setenv.setValue("TASKINSTANCEID", "%!taskinstanceid");
  _setenv.setValue("FQTN", "%!fqtn");
  _setenv.setValue("TASKGROUPID", "%!taskgroupid");
  _setenv.setValue("TASKID", "%!taskid");
  QList<Logger*> loggers;
  QList<LogFile> logfiles;
  foreach (PfNode node, root.childrenByName("log")) {
    QString level = node.attribute("level");
    QString filename = node.attribute("file");
    bool buffered = !node.hasChild("unbuffered");
    if (level.isEmpty()) {
      Log::warning() << "invalid log level in configuration: "
                     << node.toPf();
    } else if (filename.isEmpty()) {
      Log::warning() << "invalid log filename in configuration: "
                     << node.toPf();
    } else {
      Log::debug() << "adding logger " << node.toPf();
      Log::Severity minimumSeverity = Log::severityFromString(level);
      loggers.append(new FileLogger(filename, minimumSeverity, buffered));
      logfiles.append(LogFile(filename, minimumSeverity, buffered));
    }
  }
  emit logConfigurationChanged(logfiles);
  //Log::debug() << "replacing loggers " << loggers.size();
  Log::replaceLoggersPlusConsole(Log::Fatal, loggers);
  ConfigUtils::loadParamSet(root, &_globalParams);
  ConfigUtils::loadSetenv(root, &_setenv);
  ConfigUtils::loadUnsetenv(root, &_unsetenv);
  _calendars.clear();
  foreach (PfNode node, root.childrenByName("calendar")) {
    QString name = node.contentAsString();
    Calendar calendar(node);
    if (name.isEmpty())
      Log::error() << "ignoring anonymous calendar: " << node.toPf();
    else if (calendar.isNull())
      Log::error() << "ignoring empty calendar: " << node.toPf();
    else
      _calendars.insert(name, calendar);
  }
  _tasksGroups.clear();
  foreach (PfNode node, root.childrenByName("taskgroup")) {
    TaskGroup taskGroup(node, _globalParams, _setenv, _unsetenv, this);
    QString id = taskGroup.id();
    if (taskGroup.isNull() || id.isEmpty()) {
      Log::error() << "ignoring invalid taskgroup: " << node.toPf();
      goto ignore_taskgroup;
    }
    if (_tasksGroups.contains(id)) {
      Log::error() << "ignoring duplicate taskgroup " << id;
      goto ignore_taskgroup;
    }
    _tasksGroups.insert(taskGroup.id(), taskGroup);
ignore_taskgroup:;
  }
  QHash<QString,Task> oldTasks = _tasks;
  _tasks.clear();
  foreach (PfNode node, root.childrenByName("task")) {
    QString taskGroupId = node.attribute("taskgroup");
    TaskGroup taskGroup = _tasksGroups.value(taskGroupId);
    Task task(node, this, taskGroup, oldTasks, Task());
    if (taskGroupId.isEmpty() || taskGroup.isNull()) {
      Log::error() << "ignoring task with invalid taskgroup: " << node.toPf();
      goto ignore_task;
    }
    if (task.isNull()) { // Task cstr detected an error
      Log::error() << "ignoring invalid task: " << node.toString();
      goto ignore_task;
    }
    if (_tasks.contains(task.fqtn())) {
      Log::error() << "ignoring duplicate task " << task.fqtn();
      goto ignore_task;
    }
    foreach (Step s, task.steps()) { // check for uniqueness of subtasks ids
      Task subtask = s.subtask();
      if (!subtask.isNull()) {
        QString subFqtn = subtask.fqtn();
        if (_tasks.contains(subFqtn)) {
          Log::error() << "ignoring task " << task.fqtn()
                       << " since its subtask " << subFqtn
                       << " has a duplicate id";
          goto ignore_task;
        }
      }
    }
    _tasks.insert(task.fqtn(), task);
    foreach (Step s, task.steps()) {
      Task subtask = s.subtask();
      if (!subtask.isNull())
        _tasks.insert(subtask.fqtn(), subtask);
    }
ignore_task:;
  }
  _hosts.clear();
  foreach (PfNode node, root.childrenByName("host")) {
    Host host(node);
    if (_hosts.contains(host.id())) {
      Log::error() << "ignoring duplicate host: " << host.id();
    } else {
      _hosts.insert(host.id(), host);
      _resources.insert(host.id(), host.resources());
    }
  }
  _clusters.clear();
  foreach (PfNode node, root.childrenByName("cluster")) {
    Cluster cluster(node);
    foreach (PfNode child, node.childrenByName("host")) {
      Host host = _hosts.value(child.contentAsString());
      if (!host.isNull())
        cluster.appendHost(host);
      else
        Log::error() << "host '" << child.contentAsString()
                     << "' not found, won't add it to cluster '"
                     << cluster.id() << "'";
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
  int maxtotaltaskinstances = 0;
  foreach (PfNode node, root.childrenByName("maxtotaltaskinstances")) {
    int n = node.contentAsString().toInt(0, 0);
    if (n > 0) {
      if (maxtotaltaskinstances > 0) {
        Log::warning() << "overriding maxtotaltaskinstances "
                       << maxtotaltaskinstances << " with " << n;
      }
      maxtotaltaskinstances = n;
    } else {
      Log::warning() << "ignoring maxtotaltaskinstances with incorrect "
                        "value: " << node.toPf();
    }
  }
  if (maxtotaltaskinstances <= 0) {
    Log::debug() << "configured 16 task executors (default "
                    "maxtotaltaskinstances value)";
    maxtotaltaskinstances = 16;
  }
  int executorsToAdd = maxtotaltaskinstances - _maxtotaltaskinstances;
  if (executorsToAdd < 0) {
    if (-executorsToAdd > _availableExecutors.size()) {
      Log::warning() << "cannot set maxtotaltaskinstances down to "
                     << maxtotaltaskinstances << " because there are too "
                        "currently many busy executors, setting it to "
                     << maxtotaltaskinstances
                        - (executorsToAdd - _availableExecutors.size())
                     << " instead";
      maxtotaltaskinstances -= executorsToAdd - _availableExecutors.size();
      executorsToAdd = -_availableExecutors.size();
    }
    Log::debug() << "removing " << -executorsToAdd << " executors to reach "
                    "maxtotaltaskinstances of " << maxtotaltaskinstances;
    for (int i = 0; i < -executorsToAdd; ++i)
      _availableExecutors.takeFirst()->deleteLater();
  } else if (executorsToAdd > 0) {
    Log::debug() << "adding " << executorsToAdd << " executors to reach "
                    "maxtotaltaskinstances of " << maxtotaltaskinstances;
    for (int i = 0; i < executorsToAdd; ++i) {
      Executor *e = new Executor(_alerter);
      connect(e, SIGNAL(taskFinished(TaskInstance,QPointer<Executor>)),
              this, SLOT(taskFinishing(TaskInstance,QPointer<Executor>)));
      connect(e, SIGNAL(taskStarted(TaskInstance)),
              this, SIGNAL(taskStarted(TaskInstance)));
      connect(this, SIGNAL(noticePosted(QString,ParamSet)),
              e, SLOT(noticePosted(QString,ParamSet)));
      _availableExecutors.append(e);
    }
  } else {
    Log::debug() << "keep maxtotaltaskinstances of " << maxtotaltaskinstances;
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
  Log::debug() << "setting maxqueuedrequests to " << maxqueuedrequests;
  QList<PfNode> alerts = root.childrenByName("alerts");
  if (alerts.size() > 1)
    Log::warning() << "ignoring all but last duplicated alerts configuration";
  if (alerts.size()) {
    if (!_alerter->loadConfiguration(alerts.last()))
      return false;
  } else {
    if (!_alerter->loadConfiguration(PfNode("alerts")))
      return false;
  }
  _onstart.clear();
  foreach (PfNode node, root.childrenByName("onstart"))
    loadEventSubscription("*", node, &_onstart, "");
  _onsuccess.clear();
  foreach (PfNode node, root.childrenByName("onsuccess"))
    loadEventSubscription("*", node, &_onsuccess, "");
  _onfailure.clear();
  foreach (PfNode node, root.childrenByName("onfailure"))
    loadEventSubscription("*", node, &_onfailure, "");
  foreach (PfNode node, root.childrenByName("onfinish")) {
    loadEventSubscription("*", node, &_onsuccess, "");
    loadEventSubscription("*", node, &_onfailure, "");
  }
  _onlog.clear();
  foreach (PfNode node, root.childrenByName("onlog"))
    loadEventSubscription("*", node, &_onlog, "");
  _onnotice.clear();
  foreach (PfNode node, root.childrenByName("onnotice"))
    loadEventSubscription("*", node, &_onnotice, "");
  _onschedulerstart.clear();
  foreach (PfNode node, root.childrenByName("onschedulerstart"))
    loadEventSubscription("*", node, &_onschedulerstart, "");
  _onconfigload.clear();
  foreach (PfNode node, root.childrenByName("onconfigload"))
    loadEventSubscription("*", node, &_onconfigload, "");
  // LATER onschedulershutdown
  foreach (const RequestTaskActionLink &link, _requestTaskActionLinks) {
    QString id = link._action.targetName();
    QString fqtn(link._contextTask.fqtn());
    if (!link._contextTask.isNull() && !id.contains('.')) { // id to group
      id = fqtn.left(fqtn.lastIndexOf('.')+1)+id;
    }
    Task t(_tasks.value(id));
    if (!t.isNull()) {
      QString context(link._contextLabel);
      if (!link._contextTask.isNull())
        context = fqtn;
      if (context.isEmpty())
        context = "*";
      t.appendOtherTriggers("*"+link._eventType+"("+context+")");
      //Log::fatal() << "*** " << t.otherTriggers() << " *** " << link._contextLabel << " *** " << fqtn;
      _tasks.insert(id, t);
    } else
      Log::debug() << "cannot translate event " << link._eventType
                   << " for task '" << id << "'";
  }
  _requestTaskActionLinks.clear();
  _authenticator->clearUsers();
  _usersDatabase->clearUsers();
  bool accessControlEnabled = false;
  if (_accessControlFilesWatcher)
    _accessControlFilesWatcher->deleteLater();
  _accessControlFilesWatcher = new QFileSystemWatcher(this);
  connect(_accessControlFilesWatcher, SIGNAL(fileChanged(QString)),
          this, SLOT(reloadAccessControlConfig()));
  foreach (PfNode node, root.childrenByName("access-control")) {
    if (accessControlEnabled) {
      Log::error() << "ignoring multiple 'access-control' in configuration";
      break;
    }
    accessControlEnabled = true;
    _accessControlNode = node;
    reloadAccessControlConfig();
  }
  QMetaObject::invokeMethod(this, "checkTriggersForAllTasks",
                            Qt::QueuedConnection);
  emit tasksConfigurationReset(_tasksGroups, _tasks);
  emit targetsConfigurationReset(_clusters, _hosts);
  emit hostResourceConfigurationChanged(_resources);
  emit globalParamsChanged(_globalParams);
  emit globalSetenvChanged(_setenv);
  emit globalUnsetenvChanged(_unsetenv);
  emit eventsConfigurationReset(_onstart, _onsuccess, _onfailure, _onlog,
                                _onnotice, _onschedulerstart, _onconfigload);
  emit calendarsConfigurationReset(_calendars);
  emit accessControlConfigurationChanged(accessControlEnabled);
  emit configReloaded(); // must be last signal
  reevaluateQueuedRequests();
  // inspect queued requests to replace Task objects or remove request
  for (int i = 0; i < _queuedRequests.size(); ++i) {
    TaskInstance &r = _queuedRequests[i];
    QString fqtn = r.task().fqtn();
    Task t = _tasks.value(fqtn);
    if (t.isNull()) {
      Log::warning(fqtn, r.id())
          << "canceling queued task while reloading configuration because this "
             "task no longer exists: '" << fqtn << "'";
      r.setReturnCode(-1);
      r.setSuccess(false);
      r.setEndDatetime();
      // LATER maybe these signals should be emited asynchronously
      emit taskFinished(r);
      emit taskChanged(r.task());
      _queuedRequests.removeAt(i--);
    } else {
      Log::info(fqtn, r.id())
          << "replacing task definition in queued request while reloading "
             "configuration";
      r.setTask(t);
    }
  }
  ml.unlock();
  _configdate = QDateTime::currentDateTime().toMSecsSinceEpoch();
  if (_firstConfigurationLoad) {
    _firstConfigurationLoad = false;
    Log::info() << "starting scheduler";
    foreach(EventSubscription sub, _onschedulerstart)
      sub.triggerActions(&_globalParams);
  }
  foreach(EventSubscription sub, _onconfigload)
    sub.triggerActions(&_globalParams);
  return true;
}

void Scheduler::reloadAccessControlConfig() {
  //qDebug() << "reloadAccessControlConfig";
  foreach (PfNode node, _accessControlNode.childrenByName("user-file")) {
    QString path = node.contentAsString().trimmed();
    QString cipher = node.attribute("cipher", "password").trimmed().toLower();
    // implicitly, format is: login:crypted_password:role1,role2,rolen
    // later, other formats may be supported
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      Log::error() << "cannot open access control user file '" << path
                   << "': " << file.errorString();
      continue;
    }
    _accessControlFilesWatcher->addPath(path);
    QByteArray row;
    while (row = file.readLine(65535), !row.isNull()) {
      QString line = QString::fromUtf8(row).trimmed();
      if (line.size() == 0 || line.startsWith('#'))
        continue; // ignore empty lines and support # as a comment mark
      QStringList fields = line.split(':');
      if (fields.size() < 3) {
        Log::error() << "access control user file '" << path
                     << "' contains invalid line: " << line;
        continue;
      }
      QString id = fields[0].trimmed();
      QString password = fields[1].trimmed();
      QSet<QString> roles;
      foreach (const QString role,
               fields[2].trimmed().split(',', QString::SkipEmptyParts))
        roles.insert(role.trimmed());
      if (id.isEmpty() || password.isEmpty() || roles.isEmpty()) {
        Log::error() << "access control user file '" << path
                     << "' contains a line with empty mandatory fields: "
                     << line;
        continue;
      }
      InMemoryAuthenticator::Encoding encoding;
      if (cipher == "password" || cipher == "plain") {
        encoding = InMemoryAuthenticator::Plain;
      } else if (cipher == "md5hex") {
        encoding = InMemoryAuthenticator::Md5Hex;
      } else if (cipher == "md5" || cipher == "md5b64") {
        encoding = InMemoryAuthenticator::Md5Base64;
      } else if (cipher == "sha1" || cipher == "sha1hex") {
        encoding = InMemoryAuthenticator::Sha1Hex;
      } else if (cipher == "sha1b64") {
        encoding = InMemoryAuthenticator::Sha1Base64;
      } else if (cipher == "ldap") {
        encoding = InMemoryAuthenticator::OpenLdapStyle;
      } else {
        Log::error() << "access control user file '" << path
                     << "' with unsupported cipher type: '" << cipher << "'";
        break;
      }
      _authenticator->insertUser(id, password, encoding);
      _usersDatabase->insertUser(id, roles);
    }
    if (file.error() != QFileDevice::NoError)
      Log::error() << "error reading access control user file '" << path
                   << "': " << file.errorString();
  }
  foreach (PfNode node, _accessControlNode.childrenByName("user")) {
    QString id = node.attribute("id");
    QString password = node.attribute("password");
    QString md5 = node.attribute("md5");
    QString sha1 = node.attribute("sha1");
    QString ldap = node.attribute("ldap");
    int passwordCount = (password.isEmpty() ? 0 : 1)
        + (md5.isEmpty() ? 0 : 1)
        + (sha1.isEmpty() ? 0 : 1)
        + (ldap.isEmpty() ? 0 : 1);
    QStringList roles = node.stringListAttribute("roles");
    if (id.isEmpty())
      Log::error() << "found user with no id";
    if (passwordCount == 0)
      Log::error() << "user '" << id << "' with no password specification";
    if (passwordCount > 1)
      Log::error() << "user '" << id
                   << "' with severalpassword specifications";
    else {
      if (!password.isEmpty())
        _authenticator
            ->insertUser(id, password, InMemoryAuthenticator::Plain);
      else if (!md5.isEmpty())
        _authenticator->insertUser(id, md5, InMemoryAuthenticator::Md5Hex);
      else if (!sha1.isEmpty())
        _authenticator->insertUser(id, sha1, InMemoryAuthenticator::Sha1Hex);
      else if (!ldap.isEmpty())
        _authenticator
            ->insertUser(id, ldap, InMemoryAuthenticator::OpenLdapStyle);
      _usersDatabase->insertUser(id, roles.toSet());
      //Log::fatal() << "user: " << id << " : " << roles.toSet();
    }
  }
}

void Scheduler::loadEventSubscription(
    QString subscriberId, PfNode listnode, QList<EventSubscription> *list,
    QString contextLabel, Task contextTask) {
  if (!list)
    return;
  EventSubscription sub(subscriberId, listnode, this);
  list->append(sub);
  foreach (Action a, sub.actions()) {
    if (a.actionType() == "requesttask") {
      _requestTaskActionLinks.append( // TODO rename and merge contextLabel to subscriberId
            RequestTaskActionLink(a, listnode.name(), contextLabel,
                                 contextTask));
    }
  }
}

QList<TaskInstance> Scheduler::syncRequestTask(
    QString fqtn, ParamSet paramsOverriding, bool force,
    TaskInstance callerTask) {
  if (this->thread() == QThread::currentThread())
    return doRequestTask(fqtn, paramsOverriding, force, callerTask);
  QList<TaskInstance> requests;
  QMetaObject::invokeMethod(this, "doRequestTask",
                            Qt::BlockingQueuedConnection,
                            Q_RETURN_ARG(QList<TaskInstance>, requests),
                            Q_ARG(QString, fqtn),
                            Q_ARG(ParamSet, paramsOverriding),
                            Q_ARG(bool, force),
                            Q_ARG(TaskInstance, callerTask));
  return requests;
}

void Scheduler::asyncRequestTask(const QString fqtn, ParamSet paramsOverriding,
                                 bool force, TaskInstance callerTask) {
  QMetaObject::invokeMethod(this, "doRequestTask", Qt::QueuedConnection,
                            Q_ARG(QString, fqtn),
                            Q_ARG(ParamSet, paramsOverriding),
                            Q_ARG(bool, force),
                            Q_ARG(TaskInstance, callerTask));
}

QList<TaskInstance> Scheduler::doRequestTask(
    QString fqtn, ParamSet paramsOverriding, bool force,
    TaskInstance callerTask) {
  QMutexLocker ml(&_configMutex);
  Task task = _tasks.value(fqtn);
  Cluster cluster = _clusters.value(task.target());
  ml.unlock();
  if (task.isNull()) {
    Log::error() << "requested task not found: " << fqtn << paramsOverriding
                 << force;
    return QList<TaskInstance>();
  }
  if (!task.enabled()) {
    Log::info(fqtn) << "ignoring request since task is disabled: " << fqtn;
    return QList<TaskInstance>();
  }
  bool fieldsValidated(true);
  foreach (RequestFormField field, task.requestFormFields()) {
    QString name(field.param());
    if (paramsOverriding.contains(name)) {
      QString value(paramsOverriding.value(name));
      if (!field.validate(value)) {
        Log::error() << "task " << fqtn << " requested with an invalid "
                        "parameter override: '" << name << "'' set to '"
                     << value << "' whereas format is '" << field.format()
                     << "'";
        fieldsValidated = false;
      }
    }
  }
  if (!fieldsValidated)
    return QList<TaskInstance>();
  QList<TaskInstance> requests;
  if (cluster.balancing() == "each") {
    qint64 groupId = 0;
    foreach (Host host, cluster.hosts()) {
      TaskInstance request(task, groupId, force, callerTask);
      if (!groupId)
        groupId = request.groupId();
      request.setTarget(host);
      request = enqueueRequest(request, paramsOverriding);
      if (!request.isNull())
        requests.append(request);
    }
  } else {
    TaskInstance request;
    request = enqueueRequest(TaskInstance(task, force, callerTask),
                             paramsOverriding);
    if (!request.isNull())
      requests.append(request);
  }
  if (!requests.isEmpty()) {
    reevaluateQueuedRequests();
    emit taskChanged(task);
  }
  return requests;
}

TaskInstance Scheduler::enqueueRequest(
    TaskInstance request, ParamSet paramsOverriding) {
  Task task(request.task());
  QString fqtn(task.fqtn());
  foreach (RequestFormField field, task.requestFormFields()) {
    QString name(field.param());
    if (paramsOverriding.contains(name)) {
      QString value(paramsOverriding.value(name));
      field.apply(value, &request);
    }
  }
  if (!request.force() && (!task.enabled()
      || task.discardAliasesOnStart() != Task::DiscardNone)) {
    // avoid stacking disabled task requests by canceling older ones
    for (int i = 0; i < _queuedRequests.size(); ++i) {
      const TaskInstance &r2 = _queuedRequests[i];
      if (fqtn == r2.task().fqtn() && request.groupId() != r2.groupId()) {
        Log::info(fqtn, r2.id())
            << "canceling task because another instance of the same task "
               "is queued"
            << (!task.enabled() ? " and the task is disabled" : "") << ": "
            << fqtn << "/" << request.id();
        r2.setReturnCode(-1);
        r2.setSuccess(false);
        r2.setEndDatetime();
        emit taskFinished(r2);
        _queuedRequests.removeAt(i--);
      }
    }
  }
  if (_queuedRequests.size() >= _maxqueuedrequests) {
    Log::error(fqtn, request.id())
        << "cannot queue task because maxqueuedrequests is already reached ("
        << _maxqueuedrequests << ")";
    _alerter->raiseAlert("scheduler.maxqueuedrequests.reached");
    return TaskInstance();
  }
  _alerter->cancelAlert("scheduler.maxqueuedrequests.reached");
  Log::debug(fqtn, request.id())
      << "queuing task " << fqtn << "/" << request.id()
      << " with request group id " << request.groupId();
  // note: a request must always be queued even if the task can be started
  // immediately, to avoid the new tasks being started before queued ones
  _queuedRequests.append(request);
  emit taskQueued(request);
  return request;
}

TaskInstance Scheduler::cancelRequest(quint64 id) {
  if (this->thread() == QThread::currentThread())
    return doCancelRequest(id);
  TaskInstance request;
  QMetaObject::invokeMethod(this, "doCancelRequest",
                            Qt::BlockingQueuedConnection,
                            Q_RETURN_ARG(TaskInstance, request),
                            Q_ARG(quint64, id));
  return request;
}

TaskInstance Scheduler::doCancelRequest(quint64 id) {
  for (int i = 0; i < _queuedRequests.size(); ++i) {
    TaskInstance r2 = _queuedRequests[i];
    if (id == r2.id()) {
      QString fqtn(r2.task().fqtn());
      Log::info(fqtn, id) << "canceling task as requested";
      r2.setReturnCode(-1);
      r2.setSuccess(false);
      r2.setEndDatetime();
      emit taskFinished(r2);
      _queuedRequests.removeAt(i);
      return r2;
    }
  }
  Log::warning() << "cannot cancel task request because it is not (or no "
                    "longer) in requests queue";
  return TaskInstance();
}

TaskInstance Scheduler::abortTask(quint64 id) {
  if (this->thread() == QThread::currentThread())
    return doAbortTask(id);
  TaskInstance request;
  QMetaObject::invokeMethod(this, "doAbortTask",
                            Qt::BlockingQueuedConnection,
                            Q_RETURN_ARG(TaskInstance, request),
                            Q_ARG(quint64, id));
  return request;
}

TaskInstance Scheduler::doAbortTask(quint64 id) {
  QList<TaskInstance> tasks = _runningTasks.keys();
  for (int i = 0; i < tasks.size(); ++i) {
    TaskInstance r2 = tasks[i];
    if (id == r2.id()) {
      QString fqtn(r2.task().fqtn());
      Executor *executor = _runningTasks.value(r2);
      if (executor) {
        Log::warning(fqtn, id) << "aborting task as requested";
        // TODO should return TaskRequest() if executor cannot actually abort
        executor->abort();
        return r2;
      }
    }
  }
  Log::warning() << "cannot abort task because it is not in running tasks list";
  return TaskInstance();
}

void Scheduler::checkTriggersForTask(QVariant fqtn) {
  //Log::debug() << "Scheduler::checkTriggersForTask " << fqtn;
  QMutexLocker ml(&_configMutex);
  Task task = _tasks.value(fqtn.toString());
  foreach (const CronTrigger trigger, task.cronTriggers())
    checkTrigger(trigger, task, fqtn.toString());
}

void Scheduler::checkTriggersForAllTasks() {
  //Log::debug() << "Scheduler::checkTriggersForAllTasks ";
  QMutexLocker ml(&_configMutex);
  QList<Task> tasksWithoutTimeTrigger;
  foreach (Task task, _tasks.values()) {
    QString fqtn = task.fqtn();
    foreach (const CronTrigger trigger, task.cronTriggers())
      checkTrigger(trigger, task, fqtn);
    if (task.cronTriggers().isEmpty()) {
      task.setNextScheduledExecution(QDateTime());
      tasksWithoutTimeTrigger.append(task);
    }
  }
  ml.unlock();
  // LATER if this is usefull only to remove next exec time when reloading config w/o time trigger, this should be called in reloadConfig
  foreach (const Task task, tasksWithoutTimeTrigger)
    emit taskChanged(task);
}

bool Scheduler::checkTrigger(CronTrigger trigger, Task task, QString fqtn) {
  //Log::debug() << "Scheduler::checkTrigger " << trigger.cronExpression()
  //             << " " << fqtn;
  QDateTime now(QDateTime::currentDateTime());
  QDateTime next = trigger.nextTriggering();
  bool fired = false;
  if (next <= now) {
    // requestTask if trigger reached
    QList<TaskInstance> requests
        = syncRequestTask(fqtn, trigger.overridingParams());
    if (!requests.isEmpty())
      foreach (TaskInstance request, requests)
        Log::debug(fqtn, request.id())
            << "cron trigger " << trigger.humanReadableExpression()
            << " triggered task " << fqtn;
    else
      Log::debug(fqtn) << "cron trigger " << trigger.humanReadableExpression()
                       << " failed to trigger task " << fqtn;
    trigger.setLastTriggered(now);
    next = trigger.nextTriggering();
    fired = true;
  } else {
    QDateTime taskNext = task.nextScheduledExecution();
    if (taskNext.isValid() && taskNext <= next && taskNext > now) {
      //Log::debug() << "Scheduler::checkTrigger don't trigger or plan new "
      //                "check for task " << fqtn << " "
      //             << now.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
      //             << next.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
      //             << taskNext.toString("yyyy-MM-dd hh:mm:ss,zzz");
      return false; // don't plan new check if already planned
    }
  }
  if (next.isValid()) {
    // plan new check
    qint64 ms = now.msecsTo(next);
    //Log::debug() << "Scheduler::checkTrigger planning new check for task "
    //             << fqtn << " "
    //             << now.toString("yyyy-MM-dd hh:mm:ss,zzz") << " "
    //             << next.toString("yyyy-MM-dd hh:mm:ss,zzz") << " " << ms;
    // LATER one timer per trigger, not a new timer each time
    TimerWithArguments::singleShot(ms < INT_MAX ? ms : INT_MAX,
                                   this, "checkTriggersForTask", fqtn);
    task.setNextScheduledExecution(now.addMSecs(ms));
  } else {
    task.setNextScheduledExecution(QDateTime());
  }
  emit taskChanged(task);
  return fired;
}

class NoticeContext : public ParamsProvider {
public:
  QString _notice;
  ParamSet _params;
  NoticeContext(const QString notice, ParamSet params)
    : _notice(notice), _params(params) { }
  QVariant paramValue(const QString key, const QVariant defaultValue) const {
    if (key == "!notice")
      return _notice;
    return _params.paramValue(key, defaultValue);
  }
};

void Scheduler::postNotice(QString notice, ParamSet params) {
  if (notice.isNull()) {
    Log::warning() << "cannot post a null/empty notice";
    return;
  }
  QMutexLocker ml(&_configMutex);
  QHash<QString,Task> tasks = _tasks;
  ml.unlock();
  Log::debug() << "posting notice ^" << notice << " with params " << params;
  foreach (Task task, tasks.values()) {
    foreach (NoticeTrigger trigger, task.noticeTriggers()) {
      if (trigger.expression() == notice) { // LATER allow filters such as regexps
        Log::debug() << "notice " << trigger.humanReadableExpression()
                     << " triggered task " << task.fqtn();
        ParamSet overridingParams = trigger.overridingParams();
        foreach (QString key, params.keys())
          overridingParams.setValue(key, params.value(key));
        QList<TaskInstance> requests
            = syncRequestTask(task.fqtn(), overridingParams);
        if (!requests.isEmpty())
          foreach (TaskInstance request, requests)
            Log::debug(task.fqtn(), request.id())
                << "notice " << trigger.humanReadableExpression()
                << " triggered task " << task.fqtn();
        else
          Log::debug(task.fqtn())
              << "notice " << trigger.humanReadableExpression()
              << " failed to trigger task " << task.fqtn();
      }
    }
  }
  emit noticePosted(notice, params);
  NoticeContext context(notice, params);
  foreach (EventSubscription sub, _onnotice)
    sub.triggerActions(&context);
}

void Scheduler::reevaluateQueuedRequests() {
  QCoreApplication::postEvent(this,
                              new QEvent(REEVALUATE_QUEUED_REQUEST_EVENT));
}

void Scheduler::customEvent(QEvent *event) {
  if (event->type() == REEVALUATE_QUEUED_REQUEST_EVENT) {
    QCoreApplication::removePostedEvents(this, REEVALUATE_QUEUED_REQUEST_EVENT);
    startQueuedTasks();
  } else {
    QObject::customEvent(event);
  }
}

void Scheduler::startQueuedTasks() {
  for (int i = 0; i < _queuedRequests.size(); ) {
    TaskInstance r = _queuedRequests[i];
    if (startQueuedTask(r)) {
      _queuedRequests.removeAt(i);
      if (r.task().discardAliasesOnStart() != Task::DiscardNone) {
        // remove other requests of same task
        QString fqtn(r.task().fqtn());
        for (int j = 0; j < _queuedRequests.size(); ++j ) {
          TaskInstance r2 = _queuedRequests[j];
          if (fqtn == r2.task().fqtn() && r.groupId() != r2.groupId()) {
            Log::info(fqtn, r2.id())
                << "canceling task because another instance of the same task "
                   "is starting: " << fqtn << "/" << r.id();
            r2.setReturnCode(-1);
            r2.setSuccess(false);
            r2.setEndDatetime();
            emit taskFinished(r2);
            if (j < i)
              --i;
            _queuedRequests.removeAt(j--);
          }
        }
        emit taskChanged(r.task());
      }
    } else
      ++i;
  }
}

bool Scheduler::startQueuedTask(TaskInstance instance) {
  Task task(instance.task());
  QString fqtn(task.fqtn());
  Executor *executor = 0;
  if (!task.enabled())
    return false; // do not start disabled tasks
  if (_availableExecutors.isEmpty() && !instance.force()) {
    Log::info(fqtn, instance.id()) << "cannot execute task '" << fqtn
        << "' now because there are already too many tasks running "
           "(maxtotaltaskinstances reached)";
    _alerter->raiseAlert("scheduler.maxtotaltaskinstances.reached");
    return false;
  }
  _alerter->cancelAlert("scheduler.maxtotaltaskinstances.reached");
  if (instance.force())
    task.fetchAndAddInstancesCount(1);
  else if (task.fetchAndAddInstancesCount(1) >= task.maxInstances()) {
    task.fetchAndAddInstancesCount(-1);
    Log::warning() << "requested task '" << fqtn << "' cannot be executed "
                      "because maxinstances is already reached ("
                   << task.maxInstances() << ")";
    _alerter->raiseAlert("task.maxinstancesreached."+fqtn);
    return false;
  }
  _alerter->cancelAlert("task.maxinstancesreached."+fqtn);
  QString target = instance.target().id();
  if (target.isEmpty())
    target = task.target();
  QMutexLocker ml(&_configMutex);
  QList<Host> hosts;
  Host host = _hosts.value(target);
  if (host.isNull())
    hosts.append(_clusters.value(target).hosts());
  else
    hosts.append(host);
  if (hosts.isEmpty()) {
    Log::error(fqtn, instance.id()) << "cannot execute task '" << fqtn
        << "' because its target '" << target << "' is invalid";
    _alerter->raiseAlert("task.failure."+fqtn);
    instance.setReturnCode(-1);
    instance.setSuccess(false);
    instance.setEndDatetime();
    task.fetchAndAddInstancesCount(-1);
    emit taskFinished(instance);
    return true;
  }
  // LATER implement other cluster balancing methods than "first"
  // LATER implement best effort resource check for forced requests
  QHash<QString,qint64> taskResources = task.resources();
  foreach (Host h, hosts) {
    QHash<QString,qint64> hostResources = _resources.value(h.id());
    if (!instance.force()) {
      foreach (QString kind, taskResources.keys()) {
        if (hostResources.value(kind) < taskResources.value(kind)) {
          Log::info(fqtn, instance.id())
              << "lacks resource '" << kind << "' on host '" << h.id()
              << "' for task '" << task.id() << "' (need "
              << taskResources.value(kind) << ", have "
              << hostResources.value(kind) << ")";
          goto nexthost;
        }
        Log::debug(fqtn, instance.id())
            << "resource '" << kind << "' ok on host '" << h.id()
            << "' for task '" << fqtn << "'";
      }
    }
    // a host with enough resources was found
    foreach (QString kind, taskResources.keys())
      hostResources.insert(kind, hostResources.value(kind)
                            -taskResources.value(kind));
    _resources.insert(h.id(), hostResources);
    ml.unlock();
    emit hostResourceAllocationChanged(h.id(), hostResources);
    _alerter->cancelAlert("resource.exhausted."+target);
    instance.setTarget(h);
    instance.setStartDatetime();
    foreach (EventSubscription sub, _onstart)
      sub.triggerActions(instance);
    task.triggerStartEvents(instance);
    executor = _availableExecutors.takeFirst();
    if (!executor) {
      // this should only happen with force == true
      executor = new Executor(_alerter);
      executor->setTemporary();
      connect(executor, SIGNAL(taskFinished(TaskInstance,QPointer<Executor>)),
              this, SLOT(taskFinishing(TaskInstance,QPointer<Executor>)));
      connect(executor, SIGNAL(taskStarted(TaskInstance)),
              this, SIGNAL(taskStarted(TaskInstance)));
      connect(this, SIGNAL(noticePosted(QString,ParamSet)),
              executor, SLOT(noticePosted(QString,ParamSet)));
    }
    executor->execute(instance);
    ++_execCount;
    reevaluateQueuedRequests();
    _runningTasks.insert(instance, executor);
    return true;
nexthost:;
  }
  // no host has enough resources to execute the task
  ml.unlock();
  task.fetchAndAddInstancesCount(-1);
  Log::warning(fqtn, instance.id())
      << "cannot execute task '" << fqtn
      << "' now because there is not enough resources on target '"
      << target << "'";
  // LATER suffix alert with resources kind (one alert per exhausted kind)
  _alerter->raiseAlert("resource.exhausted."+target);
  return false;
}

void Scheduler::taskFinishing(TaskInstance instance,
                              QPointer<Executor> executor) {
  Task requestedTask(instance.task());
  QString fqtn(requestedTask.fqtn());
  QMutexLocker ml(&_configMutex);
  // configured and requested tasks are different if config reloaded meanwhile
  Task configuredTask(_tasks.value(fqtn));
  configuredTask.fetchAndAddInstancesCount(-1);
  if (executor) {
    Executor *e = executor.data();
    if (e->isTemporary())
      e->deleteLater(); // deleteLater() because it lives in its own thread
    else
      _availableExecutors.append(e);
  }
  _runningTasks.remove(instance);
  QHash<QString,qint64> taskResources = requestedTask.resources();
  QHash<QString,qint64> hostResources = _resources.value(instance.target().id());
  foreach (QString kind, taskResources.keys())
    hostResources.insert(kind, hostResources.value(kind)
                          +taskResources.value(kind));
  _resources.insert(instance.target().id(), hostResources);
  ml.unlock();
  if (instance.success())
    _alerter->cancelAlert("task.failure."+fqtn);
  else
    _alerter->raiseAlert("task.failure."+fqtn);
  emit hostResourceAllocationChanged(instance.target().id(), hostResources);
  // LATER try resubmit if the host was not reachable (this can be usefull with clusters or when host become reachable again)
  if (!instance.startDatetime().isNull() && !instance.endDatetime().isNull()) {
    configuredTask.setLastExecution(instance.startDatetime());
    configuredTask.setLastSuccessful(instance.success());
    configuredTask.setLastReturnCode(instance.returnCode());
    configuredTask.setLastTotalMillis(instance.totalMillis());
  }
  emit taskFinished(instance);
  emit taskChanged(configuredTask);
  if (instance.success()) {
    foreach (EventSubscription sub, _onsuccess)
      sub.triggerActions(instance);
    configuredTask.triggerSuccessEvents(instance);
  } else {
    foreach (EventSubscription sub, _onfailure)
      sub.triggerActions(instance);
    configuredTask.triggerFailureEvents(instance);
  }
  // LATER implement onstatus events
  if (configuredTask.maxExpectedDuration() < LLONG_MAX) {
    if (configuredTask.maxExpectedDuration() < instance.totalMillis())
      _alerter->raiseAlert("task.toolong."+fqtn);
    else
      _alerter->cancelAlert("task.toolong."+fqtn);
  }
  if (configuredTask.minExpectedDuration() > 0) {
    if (configuredTask.minExpectedDuration() > instance.runningMillis())
      _alerter->raiseAlert("task.tooshort."+fqtn);
    else
      _alerter->cancelAlert("task.tooshort."+fqtn);
  }
  reevaluateQueuedRequests();
}

bool Scheduler::enableTask(QString fqtn, bool enable) {
  QMutexLocker ml(&_configMutex);
  Task t = _tasks.value(fqtn);
  //Log::fatal() << "enableTask " << fqtn << " " << enable << " " << t.id();
  if (t.isNull())
    return false;
  t.setEnabled(enable);
  ml.unlock();
  if (enable)
    reevaluateQueuedRequests();
  emit taskChanged(t);
  return true;
}

void Scheduler::enableAllTasks(bool enable) {
  QMutexLocker ml(&_configMutex);
  QList<Task> tasks(_tasks.values());
  ml.unlock();
  foreach (Task t, tasks) {
    t.setEnabled(enable);
    emit taskChanged(t);
  }
  if (enable)
    reevaluateQueuedRequests();
}

bool Scheduler::taskExists(QString fqtn) {
  QMutexLocker ml(&_configMutex);
  return _tasks.contains(fqtn);
}

Task Scheduler::task(QString fqtn) {
  QMutexLocker ml(&_configMutex);
  return _tasks.value(fqtn);
}

void Scheduler::periodicChecks() {
  // detect queued or running tasks that exceeded their max expected duration
  QList<TaskInstance> currentInstances;
  currentInstances.append(_queuedRequests);
  currentInstances.append(_runningTasks.keys());
  foreach (const TaskInstance r, currentInstances) {
    const Task t(r.task());
    if (t.maxExpectedDuration() < r.liveTotalMillis())
      _alerter->raiseAlert("task.toolong."+t.fqtn());
  }
  // restart timer for triggers if any was lost, this is never usefull apart
  // if current system time goes back (which btw should never occur on well
  // managed production servers, however it with naive sysops)
  checkTriggersForAllTasks();
}

Calendar Scheduler::calendarByName(QString name) const {
  QMutexLocker ml(&_configMutex);
  return _calendars.value(name);
}

void Scheduler::activateWorkflowTransition(TaskInstance workflowTaskInstance,
                                           QString transitionId) {
  QMetaObject::invokeMethod(this, "doActivateWorkflowTransition",
                            Qt::QueuedConnection,
                            Q_ARG(TaskInstance, workflowTaskInstance),
                            Q_ARG(QString, transitionId));
}

void Scheduler::doActivateWorkflowTransition(TaskInstance workflowTaskInstance,
                                             QString transitionId) {
  Executor *executor = _runningTasks.value(workflowTaskInstance);
  if (executor)
    executor->activateWorkflowTransition(transitionId);
  else
    Log::error() << "cannot activate workflow transition on non-running "
                    "workflow " << workflowTaskInstance.task().fqtn()
                 << "/" << workflowTaskInstance.id() << ": " << transitionId;
}
