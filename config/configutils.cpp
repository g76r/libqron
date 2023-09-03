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
#include "configutils.h"
#include "eventsubscription.h"
#include "action/action.h"
#include "condition/disjunctioncondition.h"
#include "condition/taskwaitcondition.h"

ConfigUtils::ConfigUtils() {
}

static QRegularExpression whitespace("\\s");

void ConfigUtils::loadResourcesSet(
    const PfNode &parentnode, QMap<Utf8String,qint64> *resources,
    const Utf8String &attrname) {
  if (!resources)
    return;
  for (auto p: parentnode.utf8LongPairChildrenByName(attrname))
    if (p.second < 0)
      Log::warning() << "ignoring resource of kind " << p.first
                     << "with incorrect quantity " << parentnode.toString();
    else
      resources->insert(
            ConfigUtils::sanitizeId(p.first, ConfigUtils::LocalId), p.second);
  }
}

void ConfigUtils::writeParamSet(PfNode *parentnode, ParamSet params,
                                QString attrname, bool inherit) {
  if (!parentnode)
    return;
  QStringList list = params.keys(inherit).values();
  std::sort(list.begin(), list.end());
  foreach (const QString &key, list)
    parentnode->appendChild(PfNode(attrname, key+" "+params.rawValue(key)));
}

void ConfigUtils::writeEventSubscriptions(PfNode *parentnode,
                                          QList<EventSubscription> list,
                                          QStringList exclusionList) {
  foreach (EventSubscription es, list)
    if (!exclusionList.contains(es.eventName())
        && !es.actions().isEmpty())
      parentnode->appendChild(es.toPfNode());
}

static QRegularExpression unallowedInDimension("[^a-zA-Z0-9_]");
static QRegularExpression unallowedInTask("[^a-zA-Z0-9_\\-]");
static QRegularExpression unallowedInGroup("[^a-zA-Z0-9_\\-\\.]");
static QRegularExpression unallowedInHostname("[^a-zA-Z0-9\\-:\\[\\]\\.]");
static QRegularExpression multipleDots("\\.\\.+");
static QRegularExpression misplacedDot("(^\\.*)|(\\.*$)");
static QString singleDot(".");

QString ConfigUtils::sanitizeId(QString string, IdType idType) {
  string = string.trimmed();
  switch (idType) {
  case AlphanumId:
    return string.remove(unallowedInDimension);
  case LocalId:
    return string.remove(unallowedInTask);
  case FullyQualifiedId:
    return string.remove(unallowedInGroup).replace(multipleDots, singleDot)
        .remove(misplacedDot);
  case Hostname:
    return string.remove(unallowedInHostname).replace(multipleDots, singleDot);
  }
  return QString(); // should never happen
}

QRegularExpression ConfigUtils::readDotHierarchicalFilter(
    QString s, bool caseSensitive) {
  if (s.size() > 1 && s[0] == '/' && s[s.size()-1] == '/' )
    return QRegularExpression(
          s.mid(1, s.size()-2),
          caseSensitive ? QRegularExpression::NoPatternOption
                        : QRegularExpression::CaseInsensitiveOption);
  return convertDotHierarchicalFilterToRegexp(s, caseSensitive);
}

QRegularExpression ConfigUtils::convertDotHierarchicalFilterToRegexp(
    QString pattern, bool caseSensitive) {
  QString re('^');
  for (int i = 0; i < pattern.size(); ++i) {
    QChar c = pattern.at(i);
    switch (c.toLatin1()) {
    case '*':
      if (i >= pattern.size()-1 || pattern.at(i+1) != '*')
        re.append("[^.]*");
      else {
        re.append(".*");
        ++i;
      }
      break;
    case '\\':
      if (i < pattern.size()-1) {
        c = pattern.at(++i);
        switch (c.toLatin1()) {
        case '*':
        case '\\':
          re.append('\\').append(c);
          break;
        default:
          re.append("\\\\").append(c);
        }
      }
      break;
    case '.':
    case '[':
    case ']':
    case '(':
    case ')':
    case '+':
    case '?':
    case '^':
    case '$':
    case 0: // actual 0 or non-ascii
      // LATER fix regexp conversion, it is erroneous with some special chars
      re.append('\\').append(c);
      break;
    default:
      re.append(c);
    }
  }
  re.append('$');
  return QRegularExpression(
        re, caseSensitive ? QRegularExpression::NoPatternOption
                          : QRegularExpression::CaseInsensitiveOption);
}

void ConfigUtils::loadEventSubscription(
    PfNode parentNode, QString childName, QString subscriberId,
    QList<EventSubscription> *list, Scheduler *scheduler) {
  if (!list)
    return;
  foreach (PfNode listnode, parentNode.childrenByName(childName))
    list->append(EventSubscription(subscriberId, listnode, scheduler));
}

void ConfigUtils::loadComments(
    PfNode node, QStringList *commentsList, QSet<QString> excludedDescendants,
    int maxDepth) {
  if (!commentsList)
    return;
  int newMaxDepth = maxDepth < 0 ? maxDepth : (maxDepth-1);
  foreach (const PfNode &child, node.children()) {
    if (child.isComment())
      commentsList->append(child.contentAsUtf16());
    if (maxDepth && !excludedDescendants.contains(child.name()))
      loadComments(child, commentsList, newMaxDepth);
  }
}

void ConfigUtils::writeComments(PfNode *node, QStringList commentsList) {
  if (!node)
    return;
  foreach (const QString &comment, commentsList) {
    node->appendCommentChild(comment);
  }
}

void ConfigUtils::writeConditions(
    PfNode *parentnode, QString attrname, DisjunctionCondition conditions) {
  if (!parentnode || conditions.isEmpty())
    return;
  PfNode childnode(attrname);
  childnode.appendChildren(conditions.toPfNodes());
  parentnode->appendChild(childnode);
}
