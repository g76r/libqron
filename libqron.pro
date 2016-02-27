# Copyright 2012-2015 Hallowyn and others.
# This file is part of qron, see <http://qron.eu/>.
# Qron is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Qron is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
# You should have received a copy of the GNU Affero General Public License
# along with qron.  If not, see <http://www.gnu.org/licenses/>.

QT       += core network
QT       -= gui

CONFIG += console largefile c++11
CONFIG -= app_bundle

TEMPLATE = lib
TARGET = qron
DEFINES += LIBQRON_LIBRARY

INCLUDEPATH += ../libqtpf ../libqtssu
win32:debug:LIBS += -L../build-libqtpf-windows/debug \
  -L../build-libqtssu-windows/debug
win32:release:LIBS += -L../build-libqtpf-windows/release \
  -L../build-libqtssu-windows/release
unix:LIBS += -L../libqtpf -L../libqtssu
LIBS += -lqtpf -lqtssu

exists(/usr/bin/ccache):QMAKE_CXX = ccache g++
exists(/usr/bin/ccache):QMAKE_CXXFLAGS += -fdiagnostics-color=always
QMAKE_CXXFLAGS += -Wextra
#QMAKE_CXXFLAGS += -std=gnu++11
#QMAKE_CXXFLAGS += -fno-elide-constructors
unix:debug:QMAKE_CXXFLAGS += -ggdb

unix {
  OBJECTS_DIR = ../build-libqron-unix/obj
  RCC_DIR = ../build-libqron-unix/rcc
  MOC_DIR = ../build-libqron-unix/moc

  #autodoc.commands = (grep -v ^INPUT.= ../autodoc/Doxyfile; echo "INPUT = $(SOURCES) $(HEADERS)") | doxygen -
  #autodoc.target = ../autodoc/html/index.html
  #autodoc.depends = $(SOURCES) $(HEADERS)
  #QMAKE_EXTRA_TARGETS += autodoc
  #PRE_TARGETDEPS += $$autodoc.target
}

contains(QT_VERSION, ^4\\..*) {
  message("Cannot build with Qt version $${QT_VERSION}.")
  error("Use Qt 5.")
}

SOURCES += \
    config/task.cpp \
    config/taskgroup.cpp \
    sched/scheduler.cpp \
    trigger/crontrigger.cpp \
    config/host.cpp \
    sched/taskinstance.cpp \
    sched/executor.cpp \
    config/cluster.cpp \
    ui/clustersmodel.cpp \
    ui/hostsresourcesavailabilitymodel.cpp \
    alert/alerter.cpp \
    alert/alert.cpp \
    alert/alertchannel.cpp \
    alert/mailalertchannel.cpp \
    alert/logalertchannel.cpp \
    alert/execalertchannel.cpp \
    ui/taskinstancesmodel.cpp \
    ui/tasksmodel.cpp \
    action/action.cpp \
    action/postnoticeaction.cpp \
    action/logaction.cpp \
    action/raisealertaction.cpp \
    action/cancelalertaction.cpp \
    action/emitalertaction.cpp \
    action/requesttaskaction.cpp \
    ui/schedulereventsmodel.cpp \
    ui/lastoccuredtexteventsmodel.cpp \
    ui/taskgroupsmodel.cpp \
    config/configutils.cpp \
    config/requestformfield.cpp \
    ui/resourcesconsumptionmodel.cpp \
    config/logfile.cpp \
    ui/logfilesmodel.cpp \
    config/calendar.cpp \
    ui/htmltaskitemdelegate.cpp \
    ui/htmltaskinstanceitemdelegate.cpp \
    ui/htmlalertitemdelegate.cpp \
    config/step.cpp \
    sched/stepinstance.cpp \
    config/eventsubscription.cpp \
    action/stepaction.cpp \
    action/endaction.cpp \
    trigger/trigger.cpp \
    trigger/noticetrigger.cpp \
    ui/htmlstepitemdelegate.cpp \
    config/schedulerconfig.cpp \
    config/alerterconfig.cpp \
    config/accesscontrolconfig.cpp \
    ui/graphvizdiagramsbuilder.cpp \
    ui/qronuiutils.cpp \
    configmgt/configrepository.cpp \
    configmgt/localconfigrepository.cpp \
    configmgt/confighistoryentry.cpp \
    sysutil/parametrizedudpsender.cpp \
    action/requesturlaction.cpp \
    sysutil/parametrizednetworkrequest.cpp \
    alert/urlalertchannel.cpp \
    ui/htmllogentryitemdelegate.cpp \
    ui/configsmodel.cpp \
    ui/htmlschedulerconfigitemdelegate.cpp \
    ui/confighistorymodel.cpp \
    sched/noticepseudoparamsprovider.cpp \
    config/alertsettings.cpp \
    config/alertsubscription.cpp \
    alert/gridboard.cpp \
    config/qronconfigdocumentmanager.cpp

HEADERS += \
    config/task.h \
    config/taskgroup.h \
    sched/scheduler.h \
    trigger/crontrigger.h \
    config/host.h \
    sched/taskinstance.h \
    sched/executor.h \
    config/cluster.h \
    ui/clustersmodel.h \
    ui/hostsresourcesavailabilitymodel.h \
    alert/alerter.h \
    alert/alert.h \
    alert/alertchannel.h \
    alert/mailalertchannel.h \
    alert/logalertchannel.h \
    alert/execalertchannel.h \
    ui/taskinstancesmodel.h \
    ui/tasksmodel.h \
    action/action.h \
    action/postnoticeaction.h \
    action/action_p.h \
    action/logaction.h \
    action/raisealertaction.h \
    action/cancelalertaction.h \
    action/emitalertaction.h \
    action/requesttaskaction.h \
    ui/schedulereventsmodel.h \
    ui/lastoccuredtexteventsmodel.h \
    ui/taskgroupsmodel.h \
    config/configutils.h \
    config/requestformfield.h \
    ui/resourcesconsumptionmodel.h \
    config/logfile.h \
    ui/logfilesmodel.h \
    config/calendar.h \
    ui/htmltaskitemdelegate.h \
    ui/htmltaskinstanceitemdelegate.h \
    ui/htmlalertitemdelegate.h \
    config/step.h \
    sched/stepinstance.h \
    config/eventsubscription.h \
    action/stepaction.h \
    action/endaction.h \
    trigger/trigger.h \
    trigger/trigger_p.h \
    trigger/noticetrigger.h \
    ui/graphviz_styles.h \
    ui/htmlstepitemdelegate.h \
    config/schedulerconfig.h \
    config/alerterconfig.h \
    config/accesscontrolconfig.h \
    ui/graphvizdiagramsbuilder.h \
    libqron_global.h \
    ui/qronuiutils.h \
    config/task_p.h \
    configmgt/configrepository.h \
    configmgt/localconfigrepository.h \
    configmgt/confighistoryentry.h \
    sysutil/parametrizedudpsender.h \
    action/requesturlaction.h \
    sysutil/parametrizednetworkrequest.h \
    alert/urlalertchannel.h \
    ui/htmllogentryitemdelegate.h \
    ui/configsmodel.h \
    ui/htmlschedulerconfigitemdelegate.h \
    ui/confighistorymodel.h \
    sched/noticepseudoparamsprovider.h \
    config/alertsettings.h \
    config/alertsubscription.h \
    alert/gridboard.h \
    config/qronconfigdocumentmanager.h