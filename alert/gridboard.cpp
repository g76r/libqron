/* Copyright 2015 Hallowyn and others.
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
#include "gridboard.h"
#include "config/configutils.h"
#include "util/regexpparamsprovider.h"
#include <QSharedPointer>
#include "util/stringsparamsprovider.h"

#define DEFAULT_WARNING_DELAY 0 /* 0 means disabled */

namespace {

static QString _uiHeaderNames[] = {
  "Id", // 0
  "Label",
  "Pattern",
  "Dimensions",
  "Params",
  "Warning Delay", // 5
  "Updates Counter",
  "Renders Counter",
  "Current Components Count",
  "Current Items Count",
  "Additional Info" // 10
};

enum GridStatus { Unknown, Ok, Long, Error };

inline QString statusToHumanReadableString(GridStatus status) {
  switch (status) {
  case Ok:
    return QStringLiteral("ok");
  case Long:
    return QStringLiteral("OK BUT LONG");
  case Error:
    return QStringLiteral("ERROR");
  case Unknown:
    ;
  }
  return QStringLiteral("unknown");
}

inline QString statusToHtmlHumanReadableString(GridStatus status) {
  switch (status) {
  case Ok:
    return QStringLiteral("ok");
  case Long:
    return QStringLiteral("<stong>OK BUT LONG</stong>");
  case Error:
    return QStringLiteral("<stong>ERROR</stong>");
  case Unknown:
    ;
  }
  return QStringLiteral("unknown");
}

class TreeItem {
public:
  QString _name;
  GridStatus _status;
  QDateTime _timestamp;
  QMap<QString,TreeItem*> _children;
  TreeItem(QString name) : _name(name), _status(Unknown) { }
  void setDataFromAlert(Alert alert) {
    _timestamp = QDateTime::currentDateTime();
    switch (alert.status()) {
    case Alert::Raised:
    case Alert::Rising:
      _status = Error;
      break;
    case Alert::MayRise:
    case Alert::Dropping:
    case Alert::Canceled:
    case Alert::Nonexistent:
      _status = Ok;
      break;
    default:
      _status = Unknown;
    }
  }
  ~TreeItem() { qDeleteAll(_children); }
  void merge(const TreeItem *source) {
    foreach (const QString &dimensionValue, source->_children.keys()) {
      if (!_children.contains(dimensionValue)) {
        _children.insert(dimensionValue, new TreeItem(dimensionValue));
      }
      _children[dimensionValue]->merge(source->_children[dimensionValue]);
    }
    _status = source->_status;
    _timestamp = source->_timestamp;
  }
};

class DimensionData : public SharedUiItemData {
  friend class Dimension;
  QString _id, _rawValue, _label;
  QString id() const { return _id; }
  QString idQualifier() const { return QStringLiteral("gridboarddimension"); }
};

class Dimension : public SharedUiItem {
public:
  Dimension(PfNode node) {
    DimensionData *d = new DimensionData;
    QStringList idAndValue = node.contentAsTwoStringsList();
    d->_id = ConfigUtils::sanitizeId(
          idAndValue.value(1), ConfigUtils::AlphanumId);
    if (d->_id.isEmpty()) {
      delete d;
      return;
    }
    d->_rawValue = idAndValue.value(2);
    if (d->_rawValue.isEmpty())
      d->_rawValue = "%"+d->_id;
    d->_label = node.attribute(QStringLiteral("label"));
    setData(d);
  }
  PfNode toPfNode() const {
    const DimensionData *d = data();
    if (!d)
      return PfNode();
    QString idAndValue;
    idAndValue = d->_id+' '+d->_rawValue;
    PfNode node(QStringLiteral("dimension"), idAndValue);
    if (!d->_label.isEmpty())
      node.setAttribute(QStringLiteral("label"), d->_label);
    return node;
  }
  const DimensionData *data() const { return specializedData<DimensionData>(); }
  QString rawValue() const {
    const DimensionData *d = data();
    return d ? d->_rawValue : QString();
  }
  QString label() const {
    const DimensionData *d = data();
    return d ? (d->_label.isEmpty() ? d->_id : d->_label) : QString();
  }
};

} // unnamed namespace

Q_DECLARE_TYPEINFO(Dimension, Q_MOVABLE_TYPE);

static void mergeComponents(
    TreeItem *target, TreeItem *source,
    QList<QSharedPointer<TreeItem>> *components,
    QList<QHash<QString,TreeItem*>> *indexes) {
  // recursive merge tree
  target->merge(source);
  // replace references in indexes
  for (int i = 0; i < indexes->size(); ++i) {
    QHash<QString,TreeItem*> &index = (*indexes)[i];
    foreach (const QString &dimensionValue, index.keys())
      if (index.value(dimensionValue) == source)
        index.insert(dimensionValue, target);
  }
  // delete source
  for (int i = 0; i < components->size(); ++i)
    if (components->at(i).data() == source) {
      components->removeAt(i);
      break;
    }
}

class GridboardData : public SharedUiItemData {
public:
  QString _id, _label, _pattern, _info;
  QRegularExpression _patternRegexp;
  QList<Dimension> _dimensions;
  ParamSet _params;
  qint64 _warningDelay;
  /* Multi-dimensional data is stored as a forest of trees.
   *
   * Each tree has one level per dimension (first tree level after root for
   * first dimension and so on), with each level (but root) having
   * TreeItem::_dimensionName set and last level having too data attributes
   * set (TreeItem::_status and TreeItem::_timestamp).
   *
   * There is one tree per connected component, i.e. subset of data that share
   * at least one of their dimension value. This forest is hold by
   * GridboardData::_dataComponents.
   *
   * To determine in which tree an item lies when it has to be updated, there
   * is one index per dimension that maps a dimension value to a tree root.
   * These indexes are hold by GridboardData::_dataIndexesByDimension.
   *
   * On update, components may be merged if two of them happen to share at least
   * one dimension value whereas they used not.
   *
   * This forest structure is converted to a set of matrixes when rendering the
   * board, see Gridboard::toHtml().
   */
  /** Forest of connected components. */
  QList<QSharedPointer<TreeItem>> _dataComponents;
  /** Indexes from dimension × dimension value to component root. */
  QList<QHash<QString,TreeItem*>> _dataIndexesByDimension;
  QStringList _commentsList;
  qint64 _updatesCounter, _currentComponentsCount, _currentItemsCount;
  mutable QAtomicInteger<int> _rendersCounter;
  GridboardData() : _warningDelay(DEFAULT_WARNING_DELAY),
    _updatesCounter(0), _currentComponentsCount(0), _currentItemsCount(0) { }
  ~GridboardData() { }
  QString id() const { return _id; }
  QString idQualifier() const { return QStringLiteral("gridboard"); }
  int uiSectionCount() const {
    return sizeof _uiHeaderNames / sizeof *_uiHeaderNames; }
  QVariant uiData(int section, int role) const;
  QVariant uiHeaderData(int section, int role) const {
    return role == Qt::DisplayRole && section >= 0
        && (unsigned)section < sizeof _uiHeaderNames
        ? _uiHeaderNames[section] : QVariant();
  }
};

Gridboard::Gridboard(PfNode node, Gridboard oldGridboard,
                     ParamSet parentParams) {
  Q_UNUSED(oldGridboard)
  GridboardData *d = new GridboardData;
  d->_id = node.contentAsString();
  if (d->_id.isEmpty()) {
    Log::warning() << "gridboard with empty id: " << node.toString();
    delete d;
    return;
  }
  d->_label = node.attribute(QStringLiteral("label"));
  d->_info = node.attribute(QStringLiteral("info"));
  d->_pattern = node.attribute(QStringLiteral("pattern"));
  d->_patternRegexp = QRegularExpression(d->_pattern);
  if (!d->_patternRegexp.isValid())
    Log::warning() << "gridboard with invalid pattern: " << node.toString();
  foreach (const PfNode &child, node.childrenByName("dimension")) {
    Dimension dimension(child);
    if (dimension.isNull()) {
      Log::warning() << "gridboard " << d->_id << " with invalid dimension: "
                     << child.toString();
      continue;
    } else {
      if (d->_dimensions.contains(dimension)) {
        Log::warning() << "gridboard " << d->_id
                       << " with duplicate dimension: " << child.toString();
        continue;
      }
      d->_dimensions.append(dimension);
    }
  }
  switch (d->_dimensions.size()) {
  case 1:
    // 1 dimension gridboards get a second constant dimension with a constant
    // value
    d->_dimensions.append(Dimension(PfNode("dimension", "status status")));
    break;
  case 2:
    break; // nothing to do
  default:
    Log::warning() << "gridboard with unsupported number of dimensions ("
                   << d->_dimensions.size() << "): " << node.toString();
    delete d;
    return;
  }
  for (int i = 0; i < d->_dimensions.size(); ++i)
    d->_dataIndexesByDimension.append(QHash<QString,TreeItem*>());
  d->_warningDelay = node.doubleAttribute(
        QStringLiteral("warningdelay"), DEFAULT_WARNING_DELAY/1e3)*1e3;
  d->_params.setParent(parentParams);
  ConfigUtils::loadParamSet(node, &d->_params, QStringLiteral("param"));
  ConfigUtils::loadComments(node, &d->_commentsList);
  // LATER load old state
  // LATER load initvalues
  setData(d);
}

PfNode Gridboard::toPfNode() const {
  const GridboardData *d = data();
  if (!d)
    return PfNode();
  PfNode node(QStringLiteral("gridboard"), d->_id);
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_label.isEmpty() && d->_label != d->_id)
    node.setAttribute(QStringLiteral("label"), d->_label);
  if (!d->_info.isEmpty())
    node.setAttribute(QStringLiteral("info"), d->_info);
  node.setAttribute(QStringLiteral("pattern"), d->_pattern);
  foreach (const Dimension &dimension, d->_dimensions)
    node.appendChild(dimension.toPfNode());
  // LATER initvalues
  ConfigUtils::writeParamSet(&node, d->_params, QStringLiteral("param"));
  if (d->_warningDelay != DEFAULT_WARNING_DELAY)
    node.setAttribute(QStringLiteral("warningdelay"), d->_warningDelay/1e3);
  return node;
}

GridboardData *Gridboard::data() {
  return detachedData<GridboardData>();
}

QRegularExpression Gridboard::patternRegexp() const {
  const GridboardData *d = data();
  return d ? d->_patternRegexp : QRegularExpression();
}

void Gridboard::update(QRegularExpressionMatch match, Alert alert) {
  GridboardData *d = data();
  if (!d || d->_dimensions.isEmpty())
    return;
  // update stats
  ++d->_updatesCounter;
  // compute dimension values and identify possible roots
  QSet<TreeItem*> componentsRootsSet;
  RegexpParamsProvider rempp(match);
  QStringList dimensionValues;
  for (int i = 0; i < d->_dimensions.size(); ++i) {
    QString dimensionValue =
        d->_params.evaluate(d->_dimensions[i].rawValue(), &rempp);
    if (dimensionValue.isEmpty())
      dimensionValue = QStringLiteral("(none)");
    dimensionValues.append(dimensionValue);
    TreeItem *componentRoot =
        d->_dataIndexesByDimension[i].value(dimensionValue);
    if (componentRoot)
      componentsRootsSet.insert(componentRoot);
  }
  QList<TreeItem*> componentsRoots = componentsRootsSet.values();
  // merge roots when needed
  while (componentsRoots.size() > 1) {
    TreeItem *source = componentsRoots.takeFirst();
    mergeComponents(componentsRoots.first(), source, &d->_dataComponents,
                   &d->_dataIndexesByDimension);
    --d->_currentComponentsCount;
  }
  TreeItem *componentRoot =
      componentsRoots.size() > 0 ? componentsRoots.first() : 0;
  // create new root if needed
  if (!componentRoot) {
    componentRoot = new TreeItem(QString());
    d->_dataComponents.append(QSharedPointer<TreeItem>(componentRoot));
    d->_dataIndexesByDimension[0].insert(dimensionValues[0], componentRoot);
    ++d->_currentComponentsCount;
  }
  TreeItem *item = componentRoot;
  // walk through dimensions to create or update item
  for (int i = 0; i < d->_dimensions.size(); ++i) {
    TreeItem *child = item->_children.value(dimensionValues[i]);
    if (!child) {
      child = new TreeItem(dimensionValues[i]);
      if (i == d->_dimensions.size()-1)
        ++d->_currentItemsCount;
      d->_dataIndexesByDimension[i].insert(dimensionValues[i], componentRoot);
      item->_children.insert(dimensionValues[i], child);
    }
    item = child;
  }
  // write data
  item->setDataFromAlert(alert);
}

void Gridboard::clear() {
  GridboardData *d = data();
  d->_dataComponents.clear();
  for (int i = 0; i < d->_dataIndexesByDimension.size(); ++i)
    d->_dataIndexesByDimension[i].clear();
  d->_currentComponentsCount = 0;
  d->_currentItemsCount = 0;
}

inline QString formatted(QString text, QString key, ParamSet params) {
  StringsParamsProvider slpp(text);
  return params.value(key, text, true, &slpp);
}

QString Gridboard::toHtml() const {
  const GridboardData *d = data();
  if (!d)
    return QString("Null gridboard");
  d->_rendersCounter.fetchAndAddOrdered(1);
  if (d->_dataComponents.isEmpty())
    return QString("No data so far");
  QString tableClass = d->_params.value(
        QStringLiteral("gridboard.tableclass"),
        QStringLiteral("table table-condensed table-hover"));
  QString divClass = d->_params.value(
        QStringLiteral("gridboard.divclass"),
        QStringLiteral("row gridboard-status"));
  QString componentClass = d->_params.value(
        QStringLiteral("gridboard.componentclass"),
        QStringLiteral("gridboard-component"));
  QString tdClassOk = d->_params.value(QStringLiteral("gridboard.tdclass.ok"));
  QString tdClassWarning = d->_params.value(
        QStringLiteral("gridboard.tdclass.warning"), QStringLiteral("warning"));
  QString tdClassError = d->_params.value(
        QStringLiteral("gridboard.tdclass.error"), QStringLiteral("danger"));
  QString tdClassUnknown = d->_params.value(
        QStringLiteral("gridboard.tdclass.unknown"));
  QString s;
  QList<QSharedPointer<TreeItem>> componentRoots = d->_dataComponents;
  // sort components in the order of their first child
  std::sort(componentRoots.begin(), componentRoots.end(),
      [](const QSharedPointer<TreeItem> &a, const QSharedPointer<TreeItem> &b)
            -> bool {
    bool result =
        !b->_children.isEmpty() // b cannot be greater if empty
        && (a->_children.isEmpty() // a is lower if empty
            || a->_children.first()->_name < b->_children.first()->_name);
    return result;
  });
  s = s+"<div class=\""+divClass+"\">\n";
  foreach (QSharedPointer<TreeItem> componentRoot, componentRoots) {
    QDateTime now = QDateTime::currentDateTime();
    QHash<QString,QHash<QString, TreeItem*> > matrix;
    QStringList rows, columns;
    switch (d->_dimensions.size()) {
    case 0:
      return "Gridboard with 0 dimensions.";
    case 2: {
      QSet<QString> columnsSet;
      foreach (TreeItem *treeItem1, componentRoot->_children) {
        rows.append(treeItem1->_name);
        foreach (TreeItem *treeItem2, treeItem1->_children) {
          matrix[treeItem1->_name][treeItem2->_name] = treeItem2;
          columnsSet.insert(treeItem2->_name);
        }
      }
      columns = columnsSet.values();
      std::sort(columns.begin(), columns.end());
      break;
    }
    default:
      // LATER support more than 2 dimensions
      return "Gridboard do not yet support more than 2 dimensions.";
    }
    s = s+"<div class=\""+componentClass+"\"><table class=\""+tableClass
        +"\" id=\"gridboard."+d->_id+"\"><tr><th>&nbsp;</th>";
    foreach (const QString &column, columns)
      s = s+"<th>"
          +formatted(column, QStringLiteral("gridboard.columnformat"),
                     d->_params)+"</th>";
    foreach (const QString &row, rows) {
      s = s+"</tr><tr><th>"
          +formatted(row, QStringLiteral("gridboard.rowformat"), d->_params)
          +"</th>";
      foreach (const QString &column, columns) {
        TreeItem *item = matrix[row][column];
        GridStatus status = item ? item->_status : Unknown;
        if (status == Ok && item && d->_warningDelay &&
            item->_timestamp.msecsTo(now) > d->_warningDelay)
          status = Long;
        QString tdClass;
        switch (status) {
        case Ok:
          tdClass = tdClassOk;
          break;
        case Long:
          tdClass = tdClassWarning;
          break;
        case Error:
          tdClass = tdClassError;
          break;
        case Unknown:
          tdClass = tdClassUnknown;
          break;
        }
        s = s+"<td class=\""+tdClass+"\">"
            +(item ? statusToHtmlHumanReadableString(status)
                     +"<br/>"+item->_timestamp.toString(
                       QStringLiteral("yyyy-MM-dd hh:mm:ss,zzz"))
                   : "")+"</td>";
      }
    }
    s += "</tr></table></div>\n";
  }
  s += "</div>";
  return s;
}

QVariant GridboardData::uiData(int section, int role) const {
  switch(role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    switch(section) {
    case 0:
      return _id;
    case 1:
      if (role == Qt::EditRole)
        return _label == _id ? QVariant() : _label;
      return _label.isEmpty() ? _id : _label;
    case 2:
      return _pattern;
    case 3: {
      // LATER optimize
      QStringList list;
      foreach (const Dimension &dimension, _dimensions) {
        list << dimension.id()+"="+dimension.rawValue();
      }
      return list.join(' ');
    }
    case 4:
      return _params.toString(false, false);
    case 5:
      return _warningDelay/1e3;
    case 6:
      return _updatesCounter;
    case 7:
      return _rendersCounter.loadRelaxed();
    case 8:
      return _currentComponentsCount;
    case 9:
      return _currentItemsCount;
    case 10:
      return _info;
    }
    break;
  default:
    ;
  }
  return QVariant();
}
