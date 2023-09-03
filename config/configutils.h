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
#ifndef CONFIGUTILS_H
#define CONFIGUTILS_H

#include "libqron_global.h"
#include "pf/pfnode.h"

class EventSubscription;
class Scheduler;
class Condition;
class DisjunctionCondition;

/** Miscellaneous tools for handling configuration */
class LIBQRONSHARED_EXPORT ConfigUtils {
public:
  enum IdType {
    AlphanumId, // used for gridboards dimension: letters, digits and _
    LocalId, // used for tasks and resources: AlphanumId plus -
    FullyQualifiedId, // used for groups, hosts and clusters: LocalId plus .
    Hostname // used for hostnames; disallows _ but allows - : [ ] and .
  };
  static void loadResourcesSet(
      const PfNode &parentnode, QMap<Utf8String, qint64> *resources,
      const Utf8String &attrname);
  inline static QMap<Utf8String,qint64> loadResourcesSet(
      const PfNode &parentnode, const Utf8String &attrname) {
    QMap<Utf8String,qint64> resources;
    loadResourcesSet(parentnode, &resources, attrname);
    return resources; }
  /** For identifier, with or without dot. Cannot contain ParamSet interpreted
   * expressions such as %!yyyy. */
  static QString sanitizeId(QString string, IdType idType);
  /** Interpret s as regexp if it starts and ends with a /, else as
   * dot-hierarchical globing with * meaning [^.]* and ** meaning .* */
  static QRegularExpression readDotHierarchicalFilter(
      QString s, bool caseSensitive = true);
  static void loadEventSubscription(
      PfNode parentNode, QString childName, QString subscriberId,
      QList<EventSubscription> *list, Scheduler *scheduler);
  static void writeParamSet(PfNode *parentnode, ParamSet params,
                            QString attrname, bool inherit = false);
  /** @param exclusionList may be used e.g. to avoid double "onfinish" which
   * are both in onsuccess and onfailure event subscriptions */
  static void writeEventSubscriptions(
      PfNode *parentnode, QList<EventSubscription> list,
      QStringList exclusionList = QStringList());
  /** Recursively load comments children of node into commentsList.
   * @param excludedDescendants descendant nodes names ignored during recursion
   * @param maxDepth -1 means infinite recursion */
  static void loadComments(PfNode node, QStringList *commentsList,
                           QSet<QString> excludedDescendants = QSet<QString>(),
                           int maxDepth = -1);
  /** Convenience method. */
  static void loadComments(PfNode node, QStringList *commentsList,
                           int maxDepth) {
    loadComments(node, commentsList, QSet<QString>(), maxDepth); }
  static void writeComments(PfNode *node, QStringList commentsList);
  /** @return true if valid or absent, false if present and invalid */
  template<typename T>
  static bool loadAttribute(
      PfNode node, QString attributeName, T *field,
      std::function<T(QString value)> convert
      = [](QString value) -> T { return value; },
      std::function<bool(T value)> isValid
      = [](T) { return true; }) {
    if (!node.hasChild(attributeName))
      return true;
    auto v = node.utf16attribute(attributeName);
    T t = convert(v);
    if (!isValid(t))
      return false;
    *field = t;
    return true;
  }
  template<typename T>
  static bool loadAttribute(
      PfNode node, QString attributeName, T *field,
      std::function<bool(T value)> isValid) {
    return loadAttribute(
          node, attributeName, field,
          [](QString value) -> T { return value; }, isValid );
  }
  static bool loadBoolean(PfNode node, QString attributeName, bool *field) {
    if (!node.hasChild(attributeName))
      return true;
    auto v = node.utf16attribute(attributeName).trimmed().toLower();
    bool ok;
    auto i = v.toLongLong(&ok);
    if (!ok) {
      if (v == "true" || v == "")
        i = 1;
      else if (v == "false")
        i = 0;
      else
        return false;
    }
    *field = i != 0;
    return true;
  }
  static void writeConditions(
      PfNode *parentnode, QString attrname, DisjunctionCondition conditions);

private:
  ConfigUtils();
  /** Convert patterns like "some.path.**" "some.*.path" "some.**.path" or
   * "some.path.with.\*.star.and.\\.backslash" into regular expressions,
   * such as "^some\.path\..*$". */
  static QRegularExpression convertDotHierarchicalFilterToRegexp(
      QString pattern, bool caseSensitive = true);
};

#endif // CONFIGUTILS_H
