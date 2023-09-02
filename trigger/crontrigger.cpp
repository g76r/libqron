/* Copyright 2012-2023 Hallowyn and others.
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
#include "crontrigger.h"
#include "trigger_p.h"

// MAYDO support for more complex cron expression such as those of Quartz
// e.g. 0#3 = third sunday, 0L = last sunday, W = working day, etc.
// MAYDO support 5 fields expressions (without seconds)
// MAYDO support 7 fields expressions (with year)
// MAYDO support text dayofweek (MON, FRI...)
// MAYDO support never-expression, currently * * * 30 2 * works but it shouldn't

#define CRON_STEP_RE "(?:(?:(?:([0-9]*)(?:-([0-9]+))?)|\\*)(?:/([0-9]+))?)"
#define CRON_FIELD_RE "\\s*(" CRON_STEP_RE "(?:\\s*,\\s*" CRON_STEP_RE ")*)\\s+"
// FIXME there is probably issues with spaces in these regexps

static const QRegularExpression _cronExpressionRE("^(?:" CRON_FIELD_RE "){6}$"),
_cronFieldRE(CRON_FIELD_RE), _cronStepRE(CRON_STEP_RE "\\s*,\\s*");

namespace {

class CronField {
  int _min, _max;
  bool *_setValues;

public:
  CronField(int min, int max) : _min(min), _max(max),
    _setValues(new bool[_max-_min+1]) { }
  CronField(const CronField &other) : _min(other._min),
    _max(other._max), _setValues(new bool[_max-_min+1]) {
    for (int i = 0; i <= _max-_min; ++i)
      _setValues[i] = other._setValues[i];
  }
  ~CronField() { delete[] _setValues; }
  void reset() { setAll(false); }
  /** Set all values at a time */
  void setAll(bool value = true) {
    //qDebug() << "       set * " << value << _min << _max;
    for (int i = 0; i <= _max-_min; ++i)
      _setValues[i] = value;
  }
  /** Set several values at a time
    * @param start first value to set, -1 means min
    * @param stop last value to set, -1 means max
    * @param modulo step between values to set (0 means 1)
    * @param value to set
    */
  void set(int start, int stop = -1, int modulo = 1, bool value = true) {
    //qDebug() << "       set" << start << stop << modulo << value << _min << _max;
    if (start == -1)
      start = _min;
    if (stop == -1)
      stop = _max;
    if (modulo < 1)
      modulo = 1;
    if (start >= _min && start <= _max && stop >= _min && stop <= _max)
      for (int i = start; i <= _max && i <= stop; i += modulo)
        _setValues[i-_min] = value;
  }
  [[nodiscard]] QString toString() const {
    for (int i = 0; i <= _max-_min; ++i)
      if (!_setValues[i])
        goto notstar;
    return "*";
notstar:
    QString s;// = QString(" (%1 %2 %3 %4)").arg(_min).arg(_max).arg(_setValues[0]).arg(_setValues[1]);
    int i, prev = 0, value = _setValues[0];
    for (i = 1; i <= _max-_min; ++i)
      if (_setValues[i] != value) {
        if (value) {
          s += ",";
          s += QString::number(prev+_min);
          if (i-prev > 1) {
            s += "-";
            s += QString::number(i+_min-1);
          }
        }
        prev = i;
        value = _setValues[i];
      }
    if (value) {
      s += ",";
      s += QString::number(prev+_min);
      if (i-prev > 1) {
        s += "-";
        s += QString::number(i+_min-1);
      }
    }
    s = s.mid(1); // remove initial comma
    return s.isEmpty() ? "ERROR" : s;
  }
  operator QString() const { return toString(); }
  [[nodiscard]] bool isNull() const {
    for (int i = 0; i <= _max-_min; ++i)
      if (_setValues[i])
        return false;
    return true;
  }
  [[nodiscard]] bool isSet(int index) const {
    return index >= _min && index <= _max && _setValues[index-_min]; }
};

} // unnamed namespace

class CronTriggerData : public TriggerData {
public:
  QString _cronExpression;
  CronField _seconds, _minutes, _hours, _days, _months, _daysofweek;
  bool _isValid;
  mutable qint64 _lastTriggered, _nextTriggering;

  explicit CronTriggerData(const QString cronExpression = {})
    : _seconds(0, 59), _minutes(0, 59), _hours(0, 23), _days(1,31),
      _months(1, 12), _daysofweek(0, 6), _isValid(false), _lastTriggered(-1),
      _nextTriggering(-1) {
    parseCronExpression(cronExpression);
  }
  QString canonicalExpression() const override {
    return u"%1 %2 %3 %4 %5 %6"_s
        .arg(_seconds, _minutes, _hours, _days, _months,_daysofweek);
  }
  QString expression() const override { return _cronExpression; }
  QString humanReadableExpression() const override {
    return "("+_cronExpression+")"; }
  QDateTime nextTriggering(QDateTime max) const;
  bool isTriggering(QDateTime timestamp) const;
  bool isValid() const override { return _isValid; }
  QString triggerType() const override { return u"cron"_s; }

private:
  void parseCronExpression(QString cronExpression);
};

CronTrigger::CronTrigger(const QString cronExpression)
  : Trigger(new CronTriggerData(cronExpression)) {
}

CronTrigger::CronTrigger(PfNode node, QMap<QByteArray, Calendar> namedCalendars)
  : Trigger(new CronTriggerData(node.contentAsString())) {
  loadConfig(node, namedCalendars);
}

CronTrigger::CronTrigger(const CronTrigger &other) : Trigger(other) {
}

CronTrigger::~CronTrigger() {
}

CronTrigger &CronTrigger::operator=(const CronTrigger &other) {
  if (this != &other)
    d.operator=(other.d);
  return *this;
}

QDateTime CronTrigger::nextTriggering(QDateTime max) const {
  const CronTriggerData *d = data();
  return d ? d->nextTriggering(max) : QDateTime();
}

bool CronTrigger::isTriggering(QDateTime timestamp) const {
  const CronTriggerData *d = data();
  return d ? d->isTriggering(timestamp) : false;
}

QDateTime CronTrigger::lastTriggered() const {
  const CronTriggerData *d = data();
  return d && d->_lastTriggered >= 0
      ? QDateTime::fromMSecsSinceEpoch(d->_lastTriggered)
      : QDateTime();
}

void CronTrigger::setLastTriggered(QDateTime lastTriggered) const {
  const CronTriggerData *d = data();
  if (d) {
    d->_lastTriggered =
        lastTriggered.isValid() ? lastTriggered.toMSecsSinceEpoch() : -1;
    d->_nextTriggering = -1;
  }
}

QDateTime CronTriggerData::nextTriggering(QDateTime max) const {
  if (!_isValid)
    return QDateTime();
  if (_nextTriggering >= 0)
    return QDateTime::fromMSecsSinceEpoch(_nextTriggering);
  qint64 effectiveLast = _lastTriggered >= 0
      ? _lastTriggered : QDateTime::currentMSecsSinceEpoch();
  QDateTime next = QDateTime
      ::fromMSecsSinceEpoch((effectiveLast/1000+1)*1000);
  while (next <= max) {
    if (!_months.isSet(next.date().month())) {
      next = next.addMonths(1);
      next.setDate(QDate(next.date().year(), next.date().month(), 1));
      next.setTime(QTime(0, 0, 0));
    } else if (!_days.isSet(next.date().day())
               ||!_daysofweek.isSet(next.date().dayOfWeek()%7)
               ||!_calendar.isIncluded(next.date())) {
      next = next.addDays(1);
      next.setTime(QTime(0, 0, 0));
    } else if (!_hours.isSet(next.time().hour())) {
      next = next.addSecs(3600);
      next.setTime(QTime(next.time().hour(), 0, 0));
    } else if (!_minutes.isSet(next.time().minute())) {
      next = next.addSecs(60);
      next.setTime(QTime(next.time().hour(), next.time().minute(), 0));
    } else if (!_seconds.isSet(next.time().second())) {
      next = next.addSecs(1);
    } else {
      //qDebug() << "        found next trigger:" << lastTrigger << next;
      _nextTriggering = next.toMSecsSinceEpoch();
      return next;
    }
  }
  return QDateTime();
}

bool CronTriggerData::isTriggering(QDateTime timestamp) const {
  if (!_months.isSet(timestamp.date().month()))
    return false;
  if (!_days.isSet(timestamp.date().day())
      ||!_daysofweek.isSet(timestamp.date().dayOfWeek()%7)
      ||!_calendar.isIncluded(timestamp.date()))
    return false;
  if (!_hours.isSet(timestamp.time().hour()))
    return false;
  if (!_minutes.isSet(timestamp.time().minute()))
    return false;
  if (!_seconds.isSet(timestamp.time().second()))
    return false;
  return true;
}

void CronTriggerData::parseCronExpression(QString cronExpression) {
  _cronExpression = cronExpression;
  _seconds.reset();
  _minutes.reset();
  _hours.reset();
  _days.reset();
  _months.reset();
  _daysofweek.reset();
  if (cronExpression.isEmpty())
    return;
  cronExpression += " "; // regexp are simpler if every field ends with a space
  if (_cronExpressionRE.match(cronExpression).hasMatch()) {
    int fieldIndex = 0;
    for (auto match: _cronFieldRE.globalMatch(cronExpression)) {
      //qDebug() << "  found cron field" << match.lastCapturedIndex() << match.capturedTexts();
      QString step = match.captured(1) + ","; // regexp are simpler if every step ends with a comma
      for (auto match: _cronStepRE.globalMatch(step)) {
        QString start = match.captured(1), stop = match.captured(2),
            modulo = match.captured(3);
        bool star = false;//(stop == "*" && !modulo.isEmpty());
        bool ok;
        int iStart = start.toInt(&ok);
        if (!ok)
          iStart = -1;
        int iStop = stop.toInt(&ok);
        if (!ok)
          iStop = modulo.isEmpty() ? iStart : -1;
        //qDebug() << "    found cron step" << step << match.lastCapturedIndex() << match.capturedTexts() << start << stop << modulo;
        switch (fieldIndex) {
        case 0:
          if (star)
            _seconds.setAll();
          else
            _seconds.set(iStart, iStop, modulo.toInt());
          break;
        case 1:
          if (star)
            _minutes.setAll();
          else
            _minutes.set(iStart, iStop, modulo.toInt());
          break;
        case 2:
          if (star)
            _hours.setAll();
          else
            _hours.set(iStart, iStop, modulo.toInt());
          break;
        case 3:
          if (star)
            _days.setAll();
          else
            _days.set(iStart, iStop, modulo.toInt());
          break;
        case 4:
          if (star)
            _months.setAll();
          else
            _months.set(iStart, iStop, modulo.toInt());
          break;
        case 5:
          if (star)
            _daysofweek.setAll();
          else {
            if (iStart > 7 || iStop > 7)
              Log::warning() << "cron trigger expression with dayofweek > 7, "
                                "will use dayofweek%7 instead: "
                             << cronExpression;
            _daysofweek.set(iStart%7, iStop%7, modulo.toInt());
          }
          break;
        }
      }
      ++fieldIndex;
    }
    _isValid = !(_seconds.isNull() || _minutes.isNull() || _hours.isNull()
                 || _days.isNull() || _months.isNull()
                 || _daysofweek.isNull());
    //qDebug() << "  cron expression parsed:" << expression()
    //         << canonicalExpression() << _isValid;
  } else {
    //qDebug() << "  unsupported cron trigger expression:" << cronExpression;
    Log::warning() << "unsupported cron trigger expression: '"
                   << cronExpression << "'";
  }
}

void CronTrigger::detach() {
  // nothing to do, calling a non const method is enough
}

const CronTriggerData *CronTrigger::data() const {
  return reinterpret_cast<const CronTriggerData*>(d.data());
}

CronTriggerData *CronTrigger::data() {
  return reinterpret_cast<CronTriggerData*>(d.data());
}

#if 0
CronTrigger t("/10 15 6 31 */2 2,3");
QDateTime dt = QDateTime::currentDateTime();
for (int i = 0; i < 100; ++i) {
  QDateTime next = t.nextTrigger(dt, dt.addYears(20));
  qDebug() << "trigger" << next;
  dt = next;
}
qDebug() << "end";
#endif
