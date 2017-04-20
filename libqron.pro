# Copyright 2012-2016 Hallowyn and others.
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

CONFIG += largefile c++11
CONFIG -= app_bundle

TEMPLATE = lib
TARGET = qron

TARGET_OS=default
unix: TARGET_OS=unix
linux: TARGET_OS=linux
android: TARGET_OS=android
macx: TARGET_OS=macx
win32: TARGET_OS=win32
BUILD_TYPE=unknown
CONFIG(debug,debug|release): BUILD_TYPE=debug
CONFIG(release,debug|release): BUILD_TYPE=release

contains(QT_VERSION, ^4\\..*) {
  message("Cannot build with Qt version $${QT_VERSION}.")
  error("Use Qt 5.")
}

DEFINES += LIBQRON_LIBRARY

exists(/usr/bin/ccache):QMAKE_CXX = ccache g++
exists(/usr/bin/ccache):QMAKE_CXXFLAGS += -fdiagnostics-color=always
QMAKE_CXXFLAGS += -Wextra -Woverloaded-virtual
#QMAKE_CXXFLAGS += -fno-elide-constructors
CONFIG(debug,debug|release):QMAKE_CXXFLAGS += -ggdb

OBJECTS_DIR = ../build-$$TARGET-$$TARGET_OS/$$BUILD_TYPE/obj
RCC_DIR = ../build-$$TARGET-$$TARGET_OS/$$BUILD_TYPE/rcc
MOC_DIR = ../build-$$TARGET-$$TARGET_OS/$$BUILD_TYPE/moc
DESTDIR = ../build-$$TARGET-$$TARGET_OS/$$BUILD_TYPE
#autodoc.commands = (grep -v ^INPUT.= ../autodoc/Doxyfile; echo "INPUT = $(SOURCES) $(HEADERS)") | doxygen -
#autodoc.target = ../autodoc/html/index.html
#autodoc.depends = $(SOURCES) $(HEADERS)
#QMAKE_EXTRA_TARGETS += autodoc
#PRE_TARGETDEPS += $$autodoc.target

# dependency libs
INCLUDEPATH += ../libqtpf ../libp6core
LIBS += \
  -L../build-qtpf-$$TARGET_OS/$$BUILD_TYPE \
  -L../build-p6core-$$TARGET_OS/$$BUILD_TYPE
LIBS += -lqtpf -lp6core

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
    config/step.cpp \
    sched/stepinstance.cpp \
    config/eventsubscription.cpp \
    action/stepaction.cpp \
    action/endaction.cpp \
    trigger/trigger.cpp \
    trigger/noticetrigger.cpp \
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
    ui/configsmodel.cpp \
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
    config/step.h \
    sched/stepinstance.h \
    config/eventsubscription.h \
    action/stepaction.h \
    action/endaction.h \
    trigger/trigger.h \
    trigger/trigger_p.h \
    trigger/noticetrigger.h \
    ui/graphviz_styles.h \
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
    ui/configsmodel.h \
    ui/confighistorymodel.h \
    sched/noticepseudoparamsprovider.h \
    config/alertsettings.h \
    config/alertsubscription.h \
    alert/gridboard.h \
    config/qronconfigdocumentmanager.h
