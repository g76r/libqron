/* Copyright 2022-2025 Hallowyn, Gregoire Barbier and others.
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
#include "eventthread.h"
#include "util/regexpparamsprovider.h"

using namespace std::chrono_literals;

EventThread::EventThread(unsigned sizePowerOf2, QObject *parent)
    : QThread(parent), _buffer(sizePowerOf2) {
}

EventThread::~EventThread() {
}

void EventThread::run() {
  while (!isInterruptionRequested()) {
    EventThread::Event e;
    if (_buffer.tryGet(&e, 500ms)) {
      if (e.isNull())
        break;
      for (const auto &sub: e._subs) {
        auto match = sub.filter().match(e._payload);
        if (!match.hasMatch())
          continue;
        RegexpParamsProvider rpp(match);
        auto ppm = ParamsProviderMerger(&rpp)(&e._context);
        if (sub.triggerActions(&ppm, e._instance))
          break;
      }
    }
  }
}
