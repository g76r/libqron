/* Copyright 2013-2021 Hallowyn and others.
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
#include "util/paramset.h"
#include <QRegularExpression>
#include <functional>

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
  /** load as param set every child content named attrname,
   * for instance (task foo(param a 1)(param a 2)) will load a=1 and b=2 with
   * attrname = "param" */
  static void loadParamSet(PfNode parentnode, ParamSet *params,
                           QString attrname);
  inline static ParamSet loadParamSet(PfNode parentnode, QString attrname) {
    ParamSet params;
    loadParamSet(parentnode, &params, attrname);
    return params; }
  /** load as paramset every child which name matches param names in attrnames
   * for instance (writefile(append true)) with
   * attrnames = { "append", "truncate" } */
  static void loadParamSet(PfNode parentnode, ParamSet *params,
                           QSet<QString> attrnames);
  inline static ParamSet loadParamSet(PfNode parentnode,
                                      QSet<QString> attrnames) {
    ParamSet params;
    loadParamSet(parentnode, &params, attrnames);
    return params; }
  static void loadFlagSet(PfNode parentnode, ParamSet *params,
                          QString attrname);
  static void loadFlagSet(PfNode parentnode, QSet<QString> *set,
                          QString attrname);
  static void loadResourcesSet(
      PfNode parentnode, QHash<QString,qint64> *resources, QString attrname);
  inline static QHash<QString,qint64> loadResourcesSet(
      PfNode parentnode, QString attrname) {
    QHash<QString,qint64> resources;
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
  static void writeFlagSet(PfNode *parentnode, QSet<QString> set,
                           QString attrname);
  static void writeFlagSet(PfNode *parentnode, ParamSet params,
                           QString attrname, bool inherit = false) {
    writeFlagSet(parentnode, params.keys(inherit), attrname);
  }
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
    auto v = node.attribute(attributeName);
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
