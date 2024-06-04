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
#ifndef GRAPHVIZ_STYLES_H
#define GRAPHVIZ_STYLES_H

#define GLOBAL_GRAPH "rankdir=LR,bgcolor=transparent,splines=polyline,ranksep=0"
#define CLUSTER_NODE "shape=box,peripheries=2,style=filled,fillcolor=\"/paired12/5\""
#define HOST_NODE "shape=box,style=filled,fillcolor=\"/paired12/5\""
#define RESOURCE_NODE "shape=egg,style=filled,fillcolor=\"/paired12/3\""
#define TASK_NODE "shape=box,style=\"rounded,filled\",fillcolor=\"/paired12/1\""
#define TASKGROUP_NODE "shape=ellipse,style=dashed"
#define TASK_TARGET_EDGE "dir=forward,arrowhead=vee"
#define CLUSTER_HOST_EDGE TASK_TARGET_EDGE
#define TASK_RESOURCE_EDGE "dir=forward,arrowhead=odot"
#define RESOURCE_HOST_EDGE "dir=back,arrowtail=dot"
#define TASKGROUP_TASK_EDGE "style=dashed"
#define TASKGROUP_EDGE "style=dashed"
#define NOTICE_NODE "shape=note,style=filled,fillcolor=\"/paired12/7\""
#define CRON_TRIGGER_NODE "shape=plain"
#define NO_TRIGGER_NODE "shape=plain"
#define TASK_TRIGGER_EDGE "dir=back,arrowtail=vee"
#define TASK_NOTRIGGER_EDGE "dir=back,arrowtail=odot,style=dashed"
#define TASK_POSTNOTICE_EDGE "dir=forward,arrowhead=vee,constraint=false"
#define TASK_PLANTASK_EDGE TASK_POSTNOTICE_EDGE ",constraint=true"
#define GLOBAL_EVENT_NODE "shape=plain"
//#define GLOBAL_EVENT_NODE "shape=pentagon"
#define GLOBAL_EDGE "dir=back,arrowhead=vee"
#define GLOBAL_POSTNOTICE_EDGE GLOBAL_EDGE ",constraint=false"
#define GLOBAL_PLANTASK_EDGE GLOBAL_EDGE
#define ANDJOIN_NODE "shape=square,label=and"
#define ORJOIN_NODE "shape=circle,label=or"
#define START_NODE "shape=circle,style=\"filled\",width=.2,label=\"\",fillcolor=black"
#define END_NODE "shape=doublecircle,style=\"filled\",width=.2,label=\"\",fillcolor=black"

#endif // GRAPHVIZ_STYLES_H
