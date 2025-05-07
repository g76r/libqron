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
#include "diagramsbuilder.h"
#include "diagrams_styles.h"
#include "action/action.h"
#include "trigger/crontrigger.h"
#include "trigger/noticetrigger.h"
#include "config/eventsubscription.h"
#include "sched/taskinstance.h"
#include "action/plantaskaction.h"
#include "condition/taskwaitcondition.h"
#include "condition/disjunctioncondition.h"
#include "sched/scheduler.h"
#include "format/svgwriter.h"
#include "config/schedulerconfig.h"

DiagramsBuilder::DiagramsBuilder() {
}

namespace {

static QString humanReadableActionEdgeLabel(
    const EventSubscription &sub, const Action &action) {
  QString label = sub.eventName();
  ParamSet params = action.params();
  auto keys = action.params().paramKeys().toSortedList();
  if (!keys.isEmpty())
    label += " ("+keys.join(',')+")";
  QString filter = sub.filter().pattern();
  if (!filter.isEmpty())
    label += "\\n"+filter;
  return label;
}

static Utf8String actionEdgeStyle(const Utf8String &cause) {
  if (cause == "onplan")
    return ",color=\"/paired12/2\",fontcolor=\"/paired12/2\"";
  if (cause == "onstart" || cause == "allsuccess")
    return ",color=\"/paired12/4\",fontcolor=\"/paired12/4\"";
  if (cause == "onfailure" || cause == "anyfailure" || cause == "anynonsuccess")
    return ",color=\"/paired12/6\",fontcolor=\"/paired12/6\"";
  if (cause == "onschedulerstart" || cause == "onconfigload"
      || cause == "onfinished" || cause == "allfinished")
    return ",color=\"/paired12/8\",fontcolor=\"/paired12/8\"";
  return {};
}

static QString instanceNodeStyle(TaskInstance instance, quint64 herdid) {
  QString style=TASK_NODE;
  switch(instance.status()) {
  case TaskInstance::Planned:
    style += " fillcolor=\"/paired12/1\"";
    break;
  case TaskInstance::Queued:
    style += " fillcolor=\"/paired12/11\"";
    break;
  case TaskInstance::Running:
  case TaskInstance::Waiting:
    style += " fillcolor=\"/paired12/3\"";
    break;
  case TaskInstance::Canceled:
      style += " fillcolor=\"grey85\"";
      break;
  case TaskInstance::Success:
    style += " fillcolor=\"white\"";
    break;
  case TaskInstance::Failure:
    style += " fillcolor=\"/paired12/5\"";
    break;
  }
  if (instance.idAsLong() == instance.herdid())
    style += " peripheries=2";
  if (instance.herdid() != herdid)
    style += " style=\"rounded,filled,dashed\"";
  return style;
}

struct WaitCondition {
  Utf8String op, expr;
  inline bool operator!() const { return !op || !expr; }
};

/** Take expr from TaskWaitCondition or unique TWC embeded in a
 *  DisjunctionCondition (which is the default in many cases) */
static inline WaitCondition taskWaitConditionExpression(Condition cond) {
  if (cond.conditionType() == "disjunction"_u8) {
    // try to open a condition list and take the only item if size is 1
    auto dc = static_cast<const DisjunctionCondition&>(cond);
    if (dc.size() != 1)
      return {};
    cond = dc.conditions().first();
  }
  if (cond.conditionType() == "taskwait"_u8) {
    // if TaskWaitCondition, use conjugate operator
    auto twc = static_cast<const TaskWaitCondition&>(cond);
    return {twc.operatorAsString(), twc.expr()};
  }
  return {};
}

} // unnamed namespace

QHash<QString,QString> DiagramsBuilder::configDiagrams(
    const SchedulerConfig &config) {
  auto tasks = config.tasks();
  auto clusters = config.clusters();
  auto hosts = config.hosts();
  QList<EventSubscription> schedulerEventsSubscriptions,
      rootEventsSubscriptions;
  for (const auto &sub: config.allEventsSubscriptions()) {
    if (sub.eventName() == "onschedulerstart"
        || sub.eventName() == "onconfigload"
        || sub.eventName() == "onnotice")
      schedulerEventsSubscriptions << sub;
    else
      rootEventsSubscriptions << sub;
  }
  QSet<QString> displayedGroups, displayedHosts, notices, taskIds,
      displayedGlobalEventsName, resourcesSet;
  QHash<QString,QString> diagrams;
  QStringList sortedResources;
  // search for:
  // * displayed groups, i.e. (i) actual groups containing at less one task
  //   and (ii) virtual parent groups (e.g. a group "foo.bar" as a virtual
  //   parent "foo" which is not an actual group but which make the rendering
  //   more readable by making  visible the dot-hierarchy tree)
  // * displayed hosts, i.e. hosts that are targets of at less one cluster
  //   or one task
  // * notices, deduced from notice triggers and postnotice events (since
  //   notices are not explicitely declared in configuration objects)
  // * task ids, which are usefull to detect inexisting tasks and avoid drawing
  //   edges to or from them
  // * displayed global event names, i.e. global event names (e.g. onstartup)
  //   with at less one displayed event subscription (e.g. plantask,
  //   postnotice, emitalert)
  // * resources defined in either tasks or hosts
  for (const Task &task: tasks.values()) {
    QString s = task.taskGroup().id();
    displayedGroups.insert(s);
    for (int i = 0; (i = s.indexOf('.', i+1)) > 0; )
      displayedGroups.insert(s.mid(0, i));
    s = task.target();
    if (!s.isEmpty())
      displayedHosts.insert(s);
    taskIds.insert(task.id());
  }
  for (const Cluster &cluster: clusters.values())
    for (const Host &host: cluster.hosts())
      if (!host.isNull())
        displayedHosts.insert(host.id());
  for (const Task &task: tasks.values()) {
    for (const NoticeTrigger &trigger: task.noticeTriggers())
      notices.insert(trigger.expression());
    for (const auto &sub: rootEventsSubscriptions + task.allEventsSubscriptions()
         + task.taskGroup().allEventSubscriptions())
      for (const Action &action: sub.actions()) {
        if (action.actionType() == "postnotice")
          notices.insert(action.targetName());
      }
  }
  for (const EventSubscription &sub: schedulerEventsSubscriptions) {
    for (const Action &action: sub.actions()) {
      QString actionType = action.actionType();
      if (actionType == "postnotice")
        notices.insert(action.targetName());
      if (actionType == "postnotice" || actionType == "plantask")
        displayedGlobalEventsName.insert(sub.eventName());
    }
  }
  for (const auto &task: tasks)
    for (const auto &key: task.resources().keys())
      resourcesSet.insert(key);
  for (const auto &host: hosts) {
    for (const auto &key: host.resources().keys())
      resourcesSet.insert(key);
  }
  sortedResources = resourcesSet.values();
  std::sort(sortedResources.begin(), sortedResources.end());
  /***************************************************************************/
  // tasks deployment diagram
  QString gv;
  gv.append("graph \"tasks deployment diagram\" {\n"
            "graph[" GLOBAL_GRAPH "]\n"
            "subgraph{graph[rank=max]\n");
  for (const Host &host: hosts.values())
    if (displayedHosts.contains(host.id()))
      gv.append("\"").append(host.id()).append("\"").append("[label=\"")
          .append(host.id()).append(" (")
          .append(host.hostname()).append(")\"," HOST_NODE "]\n");
  gv.append("}\n");
  for (const Cluster &cluster: clusters.values())
    if (!cluster.isNull()) {
      gv.append("\"").append(cluster.id()).append("\"")
          .append("[label=\"").append(cluster.id()).append("\\n(")
          .append(cluster.balancingAsString())
          .append(")\"," CLUSTER_NODE "]\n");
      for (const Host &host: cluster.hosts())
        gv.append("\"").append(cluster.id()).append("\"--\"").append(host.id())
            .append("\"[" CLUSTER_HOST_EDGE "]\n");
    }
  gv.append("subgraph{graph[rank=min]\n");
  for (const auto &id: displayedGroups) {
    if (!id.contains('.')) // root groups
      gv.append("\"").append(id).append("\" [" TASKGROUP_NODE "]\n");
  }
  gv.append("}\n");
  for (const auto &id: displayedGroups) {
    if (id.contains('.')) // non root groups
      gv.append("\"").append(id).append("\" [" TASKGROUP_NODE "]\n");
  }
  for (const auto &parent: displayedGroups) {
    for (const auto &child: displayedGroups) {
      if (child == parent+child.mid(child.lastIndexOf('.')))
        gv.append("\"").append(parent).append("\" -- \"")
            .append(child).append("\" [" TASKGROUP_EDGE "]\n");
    }
  }
  for (const Task &task: tasks.values()) {
    // draw task node and group--task edge
    gv.append("\""+task.id()+"\" [label=\""+task.localId()+"\","
              +TASK_NODE+",tooltip=\""+task.id()+"\"]\n");
    gv.append("\"").append(task.taskGroup().id()).append("\"--")
        .append("\"").append(task.id())
        .append("\" [" TASKGROUP_TASK_EDGE "]\n");
    // draw task--target edges
    gv.append("\""+task.id()+"\"--\""+task.target()+"\" [xlabel=\""
              +Task::meanAsString(task.mean())
              +"\"," TASK_TARGET_EDGE "]\n");
  }
  gv.append("}");
  diagrams.insert("tasksDeploymentDiagram", gv);
  /***************************************************************************/
  // tasks trigger diagram
  gv.clear();
  gv.append("graph \"tasks trigger diagram\" {\n"
            "graph[" GLOBAL_GRAPH "]\n"
            "subgraph{graph[rank=max]\n");
  for (const QString &cause: displayedGlobalEventsName)
    gv.append("\"$global_").append(cause).append("\" [label=\"")
        .append(cause).append("\"," GLOBAL_EVENT_NODE "]\n");
  gv.append("}\n"
            "subgraph{graph[rank=max]\n");
  for (auto notice: notices) {
    notice.remove('"');
    gv.append("\"$notice_").append(notice).append("\"")
        .append("[label=\"^").append(notice).append("\"," NOTICE_NODE "]\n");
  }
  gv.append("}\n");
  // root groups
  gv.append("subgraph{graph[rank=min]\n");
  for (const auto &id: displayedGroups) {
    if (!id.contains('.')) // root groups
      gv.append("\"").append(id).append("\" [group=\"").append(id)
          .append("\"" TASKGROUP_NODE "]\n");
  }
  gv.append("}\n");
  // other groups
  for (const auto &id: displayedGroups) {
    if (id.contains('.')) // non root groups
      gv.append("\"").append(id).append("\" [group=\"").append(id)
          .append("\"" TASKGROUP_NODE "]\n");
  }
  // groups edges
  for (const auto &parent: displayedGroups) {
    for (const auto &child: displayedGroups) {
      if (child == parent+child.mid(child.lastIndexOf('.')))
        gv.append("\"").append(parent).append("\" -- \"")
            .append(child).append("\" [" TASKGROUP_EDGE "]\n");
    }
  }
  // tasks
  int cronid = 0;
  for (const Task &task: tasks.values()) {
    // task nodes and group--task edges
    gv.append("\""+task.id()+"\" [label=\""+task.localId()+"\","
              +TASK_NODE+",group=\""+task.taskGroup().id()+"\""
              +",tooltip=\""+task.id()+"\"]\n");
    gv.append("\"").append(task.taskGroup().id()).append("\"--")
        .append("\"").append(task.id())
        .append("\" [" TASKGROUP_TASK_EDGE "]\n");
    // cron triggers
    for (const CronTrigger &cron: task.cronTriggers()) {
      gv.append("\"$cron_").append(QString::number(++cronid))
          .append("\" [label=\"(").append(cron.expression())
          .append(")\"," CRON_TRIGGER_NODE "]\n");
      gv.append("\"").append(task.id()).append("\"--\"$cron_")
          .append(QString::number(cronid))
          .append("\" [" TASK_TRIGGER_EDGE "]\n");
    }
    // notice triggers
    for (const NoticeTrigger &trigger: task.noticeTriggers())
      gv.append("\"").append(task.id()).append("\"--\"$notice_")
          .append(trigger.expression().remove('"'))
          .append("\" [" TASK_TRIGGER_EDGE "]\n");
    // no trigger pseudo-trigger
    if (task.noticeTriggers().isEmpty() && task.cronTriggers().isEmpty()
        && task.otherTriggers().isEmpty()) {
      gv.append("\"$notrigger_").append(QString::number(++cronid))
          .append("\" [label=\"no trigger\"," NO_TRIGGER_NODE "]\n");
      gv.append("\"").append(task.id()).append("\"--\"$notrigger_")
          .append(QString::number(cronid))
          .append("\" [" TASK_NOTRIGGER_EDGE "]\n");
    }
    // events defined at task level
    QSet<QString> edges;
    for (const auto &sub: rootEventsSubscriptions
         + task.allEventsSubscriptions()
         + task.taskGroup().allEventSubscriptions()) {
      for (const Action &action: sub.actions()) {
        QString actionType = action.actionType();
        if (actionType == "postnotice") {
          gv.append("\"").append(task.id()).append("\"--\"$notice_")
              .append(action.targetName().remove('"')).append("\" [xlabel=\"")
              .append(humanReadableActionEdgeLabel(sub, action).remove('"'))
              .append("\"," TASK_POSTNOTICE_EDGE
                      +actionEdgeStyle(sub.eventName())+"]\n");
        } else if (actionType == "plantask") {
          QString target = action.targetName();
          if (!target.contains('.'))
            target = task.taskGroup().id()+"."+target;
          if (taskIds.contains(target))
            edges.insert("\""+task.id()+"\"--\""+target+"\" [xlabel=\""
                         +humanReadableActionEdgeLabel(sub, action).remove('"')
                         +"\"," TASK_PLANTASK_EDGE
                         +actionEdgeStyle(sub.eventName())+"]\n");
        }
      }
    }
    for (const auto &edge: edges)
      gv.append(edge);
  }
  // events defined globally
  for (const EventSubscription &sub: schedulerEventsSubscriptions) {
    for (const Action &action: sub.actions()) {
      QString actionType = action.actionType();
      if (actionType == "postnotice") {
        gv.append("\"$notice_").append(action.targetName().remove('"'))
            .append("\"--\"$global_").append(sub.eventName())
            .append("\" [" GLOBAL_POSTNOTICE_EDGE
                    +actionEdgeStyle(sub.eventName())+",xlabel=\"")
            .append(humanReadableActionEdgeLabel(sub, action).remove('"'))
            .append("\"]\n");
      } else if (actionType == "plantask") {
        QString target = action.targetName();
        if (taskIds.contains(target)) {
          gv.append("\"").append(target).append("\"--\"$global_")
              .append(sub.eventName())
              .append("\" [" GLOBAL_PLANTASK_EDGE
                      +actionEdgeStyle(sub.eventName())+",xlabel=\"")
              .append(humanReadableActionEdgeLabel(sub, action).remove('"'))
              .append("\"]\n");
        }
      }
    }
  }
  gv.append("}");
  diagrams.insert("tasksTriggerDiagram", gv);
  /***************************************************************************/
  // tasks-resources-hosts diagram
  gv.clear();
  gv.append("graph \"tasks-resources diagram\" {\n"
            "graph[" GLOBAL_GRAPH "]\n");
  for (const auto &resource: sortedResources)
    gv.append("\"resource__").append(resource).append("\"").append("[label=\"")
        .append(resource).append("\"," RESOURCE_NODE "]\n");
  gv.append("subgraph{graph[rank=max]\n");
  for (const auto &host: hosts.values())
    if (!host.resources().isEmpty()) // display hosts with resources
      gv.append("\"").append(host.id()).append("\"").append("[label=\"")
          .append(host.id()).append(" (")
          .append(host.hostname()).append(")\"," HOST_NODE "]\n");
  gv.append("}\n");
  for (const auto &host: hosts.values()) // draw host--resources edges
      for (const auto &resource: host.resources().keys()) {
        gv.append("\"resource__").append(resource).append("\" -- \"")
            .append(host.id()).append("\" [headlabel=\"")
            .append(QString::number(host.resources().value(resource)))
            .append("\"" RESOURCE_HOST_EDGE "]\n");
      }
  gv.append("subgraph{graph[rank=min]\n");
  displayedGroups.clear();// recompute displayedGroups w/ only tasks w/ resources
  for (const auto &task: tasks.values()) {
    if (task.resources().isEmpty())
      continue;
    QString s = task.taskGroup().id();
    displayedGroups.insert(s);
    for (int i = 0; (i = s.indexOf('.', i+1)) > 0; )
      displayedGroups.insert(s.mid(0, i));
  }
  for (const auto &id: displayedGroups) {
    if (!id.contains('.')) // root groups
      gv.append("\"").append(id).append("\" [" TASKGROUP_NODE "]\n");
  }
  gv.append("}\n");
  for (const auto &id: displayedGroups) {
    if (id.contains('.')) // non root groups
      gv.append("\"").append(id).append("\" [" TASKGROUP_NODE "]\n");
  }
  for (const auto &parent: displayedGroups) {
    for (const auto &child: displayedGroups) {
      if (child == parent+child.mid(child.lastIndexOf('.')))
        gv.append("\"").append(parent).append("\" -- \"")
            .append(child).append("\" [" TASKGROUP_EDGE "]\n");
    }
  }
  for (const Task &task: tasks.values()) {
    if (task.resources().isEmpty())
      continue;
    // draw task node and group--task edge
    gv.append("\""+task.id()+"\" [label=\""+task.localId()+"\","
              +TASK_NODE+",tooltip=\""+task.id()+"\"]\n");
    gv.append("\"").append(task.taskGroup().id()).append("\"--")
        .append("\"").append(task.id())
        .append("\" [" TASKGROUP_TASK_EDGE "]\n");
    // draw task--resources edges
    for (const auto &resource: task.resources().keys()) {
      gv.append("\"").append(task.id()).append("\" -- \"resource__")
          .append(resource).append("\" [taillabel=\"")
          .append(QString::number(task.resources().value(resource)))
          .append("\"" TASK_RESOURCE_EDGE "]\n");
    }
  }
  gv.append("}");
  diagrams.insert("tasksResourcesHostsDiagram", gv);
  return diagrams;
  // LATER add a full events diagram with log, udp, etc. events
}

namespace {

class PredecessorGroup {
public:
  QString _queuecondition, _cancelcondition;
  QList<QString> _instances;
};

struct WaitConditionInstance {
  Utf8String op;
  QSet<quint64> tiis;
};

struct RelatedTasks {
  quint64 herdid;
  QMap<quint64,TaskInstance> instances;
  QMap<quint64,WaitConditionInstance> prerequisites;
};

struct VerticalLine {
  int x, y1, y2;
};

static RelatedTasks findRelatedTasks(
    Scheduler *scheduler, quint64 tii, const ParamsProvider &options) {
  quint64 herdid = scheduler->taskInstanceById(tii).herdid();
  if (!herdid)
    return {};
  auto herder = scheduler->taskInstanceById(herdid);
  // selecting instances to show on diagram
  QMap<quint64,TaskInstance> instances;
  // herder
  if (!!herder)
    instances.insert(herdid, herder);
  // its parent
  auto herderparentid = herder.parentid();
  if (herderparentid) {
    auto parent = scheduler->taskInstanceById(herderparentid);
    if (!!parent)
      instances.insert(herderparentid, parent);
  }
  // instances belonging to the herd and their parent
  auto include_parents = options.paramBool("include_parents", true);
  if (options.paramBool("include_herd", true))
    for (const auto &[taskid, tii]: herder.herdedTasksIdsPairs()) {
      auto instance = instances.value(tii);
      if (!instance)
        instance = scheduler->taskInstanceById(tii);
      if (!instance)
        continue;
      instances.insert(tii, instance);
      if (!include_parents)
        continue;
      auto parentid = instance.parentid();
      if (parentid && !instances.contains(parentid)) {
        auto parent = scheduler->taskInstanceById(parentid);
        if (!!parent)
          instances.insert(parentid, parent);
      }
    }
  // their prerequisites
  QMap<quint64,WaitConditionInstance> prerequisites; // tii -> awaited instances
  for (const auto &instance: instances) {
    auto twc = taskWaitConditionExpression(instance.queuewhen());
    if (twc.expr.isEmpty())
      continue;
    auto herder = instances.value(instance.herdid());
    if (!herder)
      continue;
    auto tiis = Utf8String{twc.expr % herder}.split(' ', Qt::SkipEmptyParts);
    for (const auto &tii: tiis) {
      auto dep = tii.toNumber<quint64>();
      if (!dep)
        continue;
      auto &twci = prerequisites[instance.idAsLong()];
      twci.op = twc.op;
      twci.tiis.insert(dep);
    }
  }
  if (options.paramBool("include_prerequisites", true))
    for (const auto &[tii,twci]: prerequisites.asKeyValueRange())
      for (const auto &dep: twci.tiis) {
        auto instance = instances.value(dep);
        if (!instance)
          instance = scheduler->taskInstanceById(tii);
        if (!instance)
          continue;
        instances.insert(dep, instance);
      }
  // their children even outside the herd
  if (options.paramBool("include_children", true))
    for (const auto &tii: instances.keys())
      for (const auto &child: instances.value(tii).children())
        if (!instances.contains(child)) {
          auto instance = scheduler->taskInstanceById(child);
          if (!!instance)
            instances.insert(child, instance);
        }
  instances.removeIf([](const std::pair<quint64,TaskInstance> &p) STATIC_LAMBDA {
    return !p.second; // should never happen
  });
  return {herdid, instances, prerequisites};
}

} // unnamed namespace

Utf8String DiagramsBuilder::herdInstanceDiagram(
    Scheduler *scheduler, quint64 tii, const ParamsProvider &options) {
  auto [herdid, instances, prerequisites]
      = findRelatedTasks(scheduler, tii, options);
  if (!herdid) // means tii is invalid
    return {};
  auto herder = instances.value(herdid);
  Utf8String gv;
  gv.append("graph herd {\n" "graph[" GLOBAL_GRAPH
            ",bgcolor=grey95,"
            "label=\"herd diagram for "_u8+herder.idSlashId()+"\"]\n"_u8);
  // drawing instance nodes
  for (const auto &instance: instances) {
    gv.append("  \""+instance.id()+"\" [label=\""+instance.task().localId()+"\n"
              +instance.id()+"\" tooltip=\""+instance.id()+"\" "
              +instanceNodeStyle(instance, herdid)+"]\n");
  }
  // drawing cause edges (and non parent cause nodes)
  gv.append("  node[shape=plain]\n"); // FIXME non instance parent nodes
  gv.append("  edge[dir=forward,arrowhead=vee]\n"); // FIXME use styles TASK_TRIGGER_EDGE
  for (const auto &instance: instances) {
    auto parent = instances.value(instance.parentid());
    if (!!parent) { // parent edges
      gv.append("  \""+parent.id()+"\" -- \""+instance.id()+"\" [label=\""
                +instance.cause()+"\""+actionEdgeStyle(instance.cause())+"]\n");
      continue;
    }
    if (parent.herdid() != herdid && instance.herdid() != herdid)
      continue; // don't display causes outside herd
    if (instance.cause().isEmpty()) [[unlikely]]
      continue; // don't display empty causes
    // non parent cause edges
    gv.append("  \""+instance.cause()+"\" -- \""+instance.id()+"\"[a=a"
              +actionEdgeStyle(instance.cause())+"]\n");
  }
  // drawing condition edges
  gv.append("  edge[" PREREQUISITE_EDGE "]\n");
  gv.append("  # instances "+Utf8String::number(instances.size())+
            " prerequisites "+Utf8String::number(prerequisites.size())+"\n");
  for (const auto &instance: instances) {
    auto twci = prerequisites[instance.idAsLong()];
    auto tiis = twci.tiis.values();
    std::sort(tiis.begin(), tiis.end());
    gv.append("  # prereq "+instance.id()+" "+Utf8String::number(tiis.size())+
              " first "+Utf8String::number(tiis.value(0))+"\n");
    for (const auto &dep: tiis)
      gv.append("  \""+Utf8String::number(dep)+"\" -- \""+instance.id()
                +"\"[label=\""+twci.op+"\""+actionEdgeStyle(twci.op)+"]\n");
  }
  gv.append("}\n");
  return gv;
}

Utf8String DiagramsBuilder::herdConfigDiagram(
    const SchedulerConfig &config, const Utf8String &taskid) {
  auto task = config.tasks()[taskid];
  if (!task)
    return {};
  config.tasks().first().onplan().first().actions().first().actionType();
  return {};
}

Utf8String DiagramsBuilder::taskInstanceChronogram(
    Scheduler *scheduler, quint64 tii, const ParamsProvider &options) {
  auto [herdid, instances, prerequisites]
      = findRelatedTasks(scheduler, tii, options);
  if (!herdid || instances.isEmpty()) // means tii is invalid
    return {};
  QDateTime min = instances.first().creationDatetime(),
      max, now = QDateTime::currentDateTime();
  for (const auto &[tii, instance]: instances.asKeyValueRange()) {
    min = std::min(instance.creationDatetime(), min);
    auto last = instance.finishDatetime();
    if (!last.isValid())
      last = now;
    max = std::max(last, max);
  }
  if (!max.isValid())
    max = min;
  auto label = options.paramRawUtf8("label");
  auto fontname = options.paramUtf8("fontname", "Sans");
  auto width = options.paramNumber<int>("width", 1920);
  auto label_width = options.paramNumber<int>("label_width", 400);
  int hmargin = 4, vmargin = 4;
  int iconsize = 8, lineh = 12, fontsize = 8;
  auto secspan = std::max(min.secsTo(max), 1LL);
  auto time_width = width-iconsize-2*hmargin-label_width;
  double pps = (double)time_width/secspan;
  SvgWriter sw;
  sw.setViewport(0, 0, width, lineh*(instances.size()+2)+2*vmargin);
  sw.drawText(hmargin, vmargin, label_width, lineh, 0,
              Utf8String{min}, SVG_NEUTRAL_COLOR, fontname, fontsize);
  sw.drawText(hmargin+time_width, vmargin, label_width, lineh, Qt::AlignRight,
              Utf8String{max}, SVG_NEUTRAL_COLOR, fontname, fontsize);
  sw.drawText(hmargin+time_width/2, vmargin, label_width, lineh,
              Qt::AlignHCenter, "chronogram for task instance "+Utf8String{tii},
              SVG_NEUTRAL_COLOR, fontname, fontsize);
  for (int i = 0; i <= 10; ++i) { // drawing time ticks
    auto x = hmargin+time_width*i/10;
    sw.drawLine(x, lineh*1.5, x, lineh*2, SVG_NEUTRAL_COLOR, 1);
  }
  int i = 0;
  auto status_icon = [](TaskInstance::TaskInstanceStatus status)
      STATIC_LAMBDA -> std::tuple<Utf8String,Utf8String,Utf8String> {
    switch (status) {
      case TaskInstance::Planned:
        return {"board22", SVG_PLANNED_COLOR, SVG_PLANNED_COLOR}; // or moon
      case TaskInstance::Queued:
        return {"blockedarrow", SVG_QUEUED_COLOR, SVG_QUEUED_COLOR};
        //return {"funnel", SVG_QUEUED_COLOR, "none"};
      case TaskInstance::Canceled:
        return {"times", SVG_NEUTRAL_COLOR, "none"};
      case TaskInstance::Running:
        return {"arrowr", SVG_RUNNING_COLOR, SVG_RUNNING_COLOR};
      case TaskInstance::Waiting:
        return {"hourglass", SVG_RUNNING_COLOR, SVG_RUNNING_COLOR};
      case TaskInstance::Failure:
        return {"square", SVG_FAILURE_COLOR, SVG_FAILURE_COLOR};
      case TaskInstance::Success:
        return {"osquare", SVG_SUCCESS_COLOR, "none"};
    }
    return {"square", SVG_NEUTRAL_COLOR, SVG_NEUTRAL_COLOR};
  };
  auto draw_segment = [min,pps,&sw,hmargin](
                      const QDateTime &end, int &xp, int &x,
                      int ym, Utf8String &color, int width,
                      const Utf8String &next_color) {
    if (end.isValid()) {
      x = hmargin+(int)(pps*min.secsTo(end));
      sw.comment("     draw_segment "+Utf8String::number(x)+" "
                 +Utf8String::number(pps*min.secsTo(end))+" "+color);
      sw.drawLine(xp, ym, x, ym, color, width);
      sw.drawLine(x, ym-2, x, ym+2, next_color, 1);
      xp = x;
      color = next_color;
    }
  };
  QMap<quint64,int> tiy; // tii -> ym
  for (const auto &[tii, instance]: instances.asKeyValueRange()) {
    QDateTime creation = instance.creationDatetime(),
        queue = instance.queueDatetime(), start = instance.startDatetime(),
        stop = instance.stopDatetime(), finish = instance.finishDatetime();
    int y0 = vmargin+lineh*(i+2), ym = y0+lineh/2;
    int x0 = hmargin+(int)(pps*min.secsTo(creation));
    int x = x0, xp = x0;
    Utf8String color = SVG_PLANNED_COLOR;
    sw.comment("taskinstance "+instance.idSlashId()
               +" creation:"+creation.toString()
               +" queue:"+queue.toString()
               +" start:"+start.toString()
               +" stop:"+stop.toString()
               +" finish:"+finish.toString());
    sw.drawLine(x0, ym-2, x0, ym+2, color, 1);
    draw_segment(queue, xp, x, ym, color, 2, SVG_QUEUED_COLOR);
    draw_segment(start, xp, x, ym, color, 2, SVG_RUNNING_COLOR);
    Utf8String finish_color = start.isValid() ? SVG_RUNNING_COLOR // waiting
                                              : SVG_CANCELED_COLOR;
    draw_segment(stop, xp, x, ym, color, 4, finish_color);
    if (finish.isValid())
      draw_segment(finish, xp, x, ym, color, 2, finish_color);
    else {
      //x = x0+secspan;
      x = width-iconsize-hmargin-label_width;
      auto linew = stop.isValid() ? 2 // waiting
                                  : 4; // running
      sw.drawLine(xp, ym, x, ym, color, linew);
    }
    if (finish.isValid())
      color = instance.success() ? SVG_SUCCESS_COLOR : SVG_FAILURE_COLOR;
    auto status = instance.status();
    auto [icon,stroke,fill] = status_icon(status);
    auto icons = SvgWriter::iconNames();
    std::reverse(icons.begin(), icons.end());
    //icon = icons.value(i % icons.size());
    sw.drawSmallIcon(x+iconsize/2, ym, icon, stroke, fill);
    //sw.drawSmallIcon(x+iconsize/2+iconsize, ym, icon, stroke, stroke);
    auto text = label.isEmpty() ? instance.task().localId()+"/"+instance.id()
                                : Utf8String{label % instance};
    // TODO make text position works correctly and without magic numbers
    sw.startAnchor(instance.id());
    sw.drawText(x+iconsize, y0-3, label_width, lineh, 0, text, SVG_LABEL_COLOR,
                fontname, fontsize);
    sw.endAnchor();
    tiy[tii] = ym;
    ++i;
  }
  QList<VerticalLine> vertical_lines;
  for (const auto &[tii,twci]: prerequisites.asKeyValueRange())
    for (const auto &dep: twci.tiis) {
      if (tiy[tii] && tiy[dep]) {
        auto instance = instances.value(tii);
        auto ts = instance.queueDatetime();
        if (!ts.isValid())
          ts = instance.creationDatetime();
        vertical_lines.append({hmargin+(int)(pps*min.secsTo(ts)), tiy[tii],
                               tiy[dep]});
      }
    }
  for (int i = 0; i < vertical_lines.size(); i++) {
    for (int j = 0; j < i; j++) {
      if (vertical_lines[i].x == vertical_lines[j].x) {
        vertical_lines[i].x += 3;
        j = 0;
      }
    }
  }
  for (const auto &line: vertical_lines) {
    sw.drawLine(line.x, line.y1, line.x, line.y2, SVG_NEUTRAL_COLOR, 1);
    if (line.y1 < line.y2) {
      sw.drawLine(line.x-2, line.y2-2, line.x, line.y2, SVG_NEUTRAL_COLOR, 1);
      sw.drawLine(line.x+2, line.y2-2, line.x, line.y2, SVG_NEUTRAL_COLOR, 1);
    } else {
      sw.drawLine(line.x-2, line.y2+2, line.x, line.y2, SVG_NEUTRAL_COLOR, 1);
      sw.drawLine(line.x+2, line.y2+2, line.x, line.y2, SVG_NEUTRAL_COLOR, 1);
    }
  }
  return sw.data();
}
