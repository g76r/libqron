/* Copyright 2022-2024 Hallowyn, Gregoire Barbier and others.
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

#include "config/eventsubscription.h"
#include "taskinstance.h"
#include "thread/circularbuffer.h"
#include <QThread>

/** Dedicated thread for processing events (i.e. triggering actions). */
class LIBQRONSHARED_EXPORT EventThread : public QThread {
  Q_OBJECT
  Q_DISABLE_COPY(EventThread)

public:
  class Event {
    friend class EventThread;
    QList<EventSubscription> _subs;
    ParamSet _context;
    TaskInstance _instance;
    QString _payload;

  public:
    Event() = default;
    Event(const QList<EventSubscription> &subs, const ParamSet &context,
          const TaskInstance &instance, const QString &payload)
      : _subs(subs), _context(context), _instance(instance), _payload(payload) {
    }
    bool isNull() const { return _instance.isNull(); }
    bool operator!() const { return !isNull(); }
  };

private:
  CircularBuffer<EventThread::Event> _buffer;

public:
  /** @param sizePowerOf2 size of buffer (e.g. 10 means 1024 slots) */
  EventThread(unsigned sizePowerOf2 = 10, QObject *parent = 0);
  ~EventThread();
  void run() override;
  bool tryPut(EventThread::Event event) { return _buffer.tryPut(event); }
};

#endif // EVENTTHREAD_H
