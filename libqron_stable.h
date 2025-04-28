/* Copyright 2023-2025 Gregoire Barbier and others.
 * This file is part of libpumpkin, see <http://libpumpkin.g76r.eu/>.
 * Libpumpkin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Libpumpkin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public License
 * along with libpumpkin.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LIBQRON_STABLE_H
#define LIBQRON_STABLE_H

// C
#include <sys/types.h>

#if defined __cplusplus

// C++
#include <random>

// Qt
#include <QDeadlineTimer>
#include <QFileSystemWatcher>
#include <QEvent>
#include <QTemporaryFile>
#include <QUdpSocket>
#include <QMetaType>
#include <QAbstractTableModel>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QMimeData>
#include <QBuffer>
#include <QFileInfo>
#include <QDir>

// libp6core
#include "util/paramsprovidermerger.h"
#include "util/regexpparamsprovider.h"
#include "util/utf8stringlist.h"
#include "util/radixtree.h"
#include "util/containerutils.h"
#include "format/stringutils.h"
#include "format/timeformats.h"
#include "csv/csvfile.h"
#include "pf/pfnode.h"
#include "pf/pfparser.h"
#include "log/log.h"
#include "log/filelogger.h"
#include "log/qterrorcodes.h"
#include "modelview/shareduiitemdocumentmanager.h"
#include "modelview/genericshareduiitem.h"
#include "modelview/templatedshareduiitemdata.h"
#include "modelview/shareduiitemlist.h"
#include "modelview/textmatrixmodel.h"
#include "thread/atomicvalue.h"
#include "thread/circularbuffer.h"
#include "thread/blockingtimer.h"
#include "io/readonlyresourcescache.h"
#include "auth/inmemoryauthenticator.h"
#include "auth/inmemoryusersdatabase.h"
#include "httpd/httprequest.h"

#endif // __cplusplus

#endif // LIBQRON_STABLE_H
