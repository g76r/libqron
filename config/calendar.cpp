/* Copyright 2013-2023 Hallowyn and others.
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
#include "calendar.h"
#include "configutils.h"
#include "modelview/templatedshareduiitemdata.h"

static QAtomicInteger<qint32> sequence = 1;
static const QRegularExpression
_dateRE("(([0-9]+)-([0-9]+)-([0-9]+))?(..)?(([0-9]+)-([0-9]+)-([0-9]+))?");

class CalendarData : public SharedUiItemDataBase<CalendarData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  struct Rule {
    QDate _begin; // QDate() means -inf
    QDate _end; // QDate() means +inf
    bool _include;
    Rule(QDate begin, QDate end, bool include)
      : _begin(begin), _end(end), _include(include) { }
    bool isIncludeAll() const {
      return _include && _begin.isNull() && _end.isNull(); }
    bool isExcludeAll() const {
      return !_include && _begin.isNull() && _end.isNull(); }
  };
  Utf8String _id, _name;
  QList<Rule> _rules;
  Utf8StringList _commentsList;
  CalendarData(const Utf8String &name = {})
    : _id(Utf8String::number(sequence.fetchAndAddOrdered(1))),
      _name(name.isEmpty() ? Utf8String{} : name) { }
  Utf8String id() const override { return _id; }
  QVariant uiData(int section, int role) const override;
  Utf8String toCommaSeparatedRulesString() const;
  inline CalendarData &append(QDate begin, QDate end, bool include);
};

Q_DECLARE_TYPEINFO(CalendarData::Rule, Q_MOVABLE_TYPE);

CalendarData &CalendarData::append(QDate begin, QDate end, bool include) {
  _rules.append(Rule(begin, end, include));
  return *this;
}

Calendar::Calendar(PfNode node) {
  CalendarData *d = new CalendarData(node.contentAsUtf8());
  bool atLessOneExclude = false;
  //qDebug() << "*** Calendar(PfNode): " << node.toPf();
  foreach(PfNode child, node.children()) {
    if (child.isComment())
      continue;
    bool include;
    //qDebug() << child.name() << ": " << child.contentAsString();
    if (child.name() == "include")
      include = true;
    else if (child.name() == "exclude") {
      include = false;
      atLessOneExclude = true;
    } else {
      Log::error() << "unsupported calendar rule '" << child.name() << "': "
                   << node.toPf();
      continue;
    }
    QStringList dates = child.contentAsStringList();
    if (dates.isEmpty()) {
      //qDebug() << "calendar date spec empty";
      d->append(QDate(), QDate(), include);
    } else
      for (auto string: dates) {
        auto match = _dateRE.match(string);
        if (match.hasMatch()) {
          QDate begin(match.captured(2).toInt(), match.captured(3).toInt(),
                      match.captured(4).toInt());
          QDate end(match.captured(7).toInt(), match.captured(8).toInt(),
                    match.captured(9).toInt());
          //qDebug() << "calendar date spec:" << begin << ".." << end;
          d->append(begin, match.captured(5).isEmpty() ? begin : end, include);
        } else {
          Log::error() << "incorrect calendar date specification: "
                       << node.toPf();
        }
      }
  }
  if (!d->_rules.isEmpty() && !d->_rules.last().isIncludeAll()
      && !atLessOneExclude) {
    // if calendar only declares "include" rules, append a final "exclude" rule
    d->append(QDate(), QDate(), false);
  }
  ConfigUtils::loadComments(node, &d->_commentsList);
  setData(d);
}

Calendar &Calendar::append(QDate begin, QDate end, bool include) {
  CalendarData *d = data();
  d->append(begin, end, include);
  return *this;
}

bool Calendar::isIncluded(QDate date) const {
  const CalendarData *d = data();
  if (!d)
    return true;
  foreach (const CalendarData::Rule &r, d->_rules) {
    if ((!r._begin.isValid() || r._begin <= date)
        && (!r._end.isValid() || r._end <= date))
      return r._include;
  }
  return d->_rules.isEmpty();
}

PfNode Calendar::toPfNode(bool useNameOnlyIfSet) const {
  const CalendarData *d = data();
  if (!d)
    return PfNode();
  PfNode node("calendar", d->_name);
  if (!d->_name.isEmpty() && useNameOnlyIfSet)
    return node;
  ConfigUtils::writeComments(&node, d->_commentsList);
  foreach (const CalendarData::Rule &r, d->_rules) {
    QString s;
    s.append(r._begin.isNull() ? "" : r._begin.toString("yyyy-MM-dd"));
    if (r._begin != r._end)
      s.append("..")
          .append(r._end.isNull() ? "" : r._end.toString("yyyy-MM-dd"));
    node.appendChild(PfNode(r._include ? "include" : "exclude", s));
  }
  return node;
}

Utf8String Calendar::toCommaSeparatedRulesString() const {
  const CalendarData *d = data();
  return d ? d->toCommaSeparatedRulesString() : Utf8String();
}

Utf8String CalendarData::toCommaSeparatedRulesString() const {
  QString s;
  foreach (const CalendarData::Rule &r, _rules) {
    s.append(r._include ? "include" : "exclude");
    if (!r._begin.isNull() || !r._end.isNull())
      s.append(" ");
    if (!r._begin.isNull())
      s.append(r._begin.toString(u"yyyy-MM-dd"_s));
    if (r._begin != r._end) {
      s.append("..");
      if (!r._end.isNull())
        s.append(r._end.toString(u"yyyy-MM-dd"_s));
    }
    s.append(", ");
  }
  if (_rules.isEmpty())
    return u"include"_s;
  s.chop(2);
  return s;
}

Utf8String Calendar::name() const {
  const CalendarData *d = data();
  return d ? d->_name : Utf8String{};
}

CalendarData *Calendar::data() {
  return detachedData<CalendarData>();
}

const CalendarData *Calendar::data() const {
  return specializedData<CalendarData>();
}

QVariant CalendarData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
  case SharedUiItem::ExternalDataRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      return _name;
    case 2:
      return toCommaSeparatedRulesString();
    default:
      ;
    }
  }
  return {};
}

const Utf8String CalendarData::_qualifier = "calendar";

const Utf8StringIndexedConstList CalendarData::_sectionNames {
  "id", // 0
  "name",
  "date_rules"
};

const Utf8StringIndexedConstList CalendarData::_headerNames {
  "Id", // 0
  "Name",
  "Date Rules"
};

