/* Copyright 2022 Hallowyn, Gregoire Barbier and others.
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
#ifndef EVENTTHREAD_H
#define EVENTTHREAD_H

#include "libqron_global.h"
#include <QThread>
#include "config/eventsubscription.h"
#include "thread/circularbuffer.h"
#include "taskinstance.h"
#include <QRegularExpressionMatch>
#include "util/paramsprovidermerger.h"
#include "util/paramset.h"

/** Dedicated thread for processing events (i.e. triggering actions). */
class LIBQRONSHARED_EXPORT EventThread : public QThread {
  Q_OBJECT
  Q_DISABLE_COPY(EventThread)

public:
  class Event {
  public:
    QList<EventSubscription> _subs;
    ParamSet _context;
    TaskInstance _instance;
    QString _payload;
    Event() { }
    Event(QList<EventSubscription> subs, ParamSet context,
          TaskInstance instance, QString payload)
        : _subs(subs), _context(context), _instance(instance),
          _payload(payload) { }
    Event(QList<EventSubscription> subs, const ParamsProviderMerger *context,
          TaskInstance instance, QString payload)
        : Event(subs, context->snapshot(), instance, payload) { }
  };

private:
  CircularBuffer<EventThread::Event> _buffer;

public:
  /** @param sizePowerOf2 size of buffer (e.g. 10 means 1024 slots) */
  EventThread(unsigned sizePowerOf2 = 10, QObject *parent = 0);
  ~EventThread();
  void run();
  bool tryPut(EventThread::Event event) { return _buffer.tryPut(event); }
};

#endif // EVENTTHREAD_H
