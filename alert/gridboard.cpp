/* Copyright 2015-2024 Hallowyn and others.
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
#include "modelview/templatedshareduiitemdata.h"
#include "util/regexpparamsprovider.h"
#include "util/paramsprovidermerger.h"

#define DEFAULT_WARNING_DELAY 0 /* 0 means disabled */

namespace {

enum GridStatus { Unknown, Ok, Long, Raised };

inline Utf8String statusToHtmlHumanReadableString(GridStatus status) {
  switch (status) {
  case Ok:
    return "ok"_u8;
  case Long:
    return "<stong>OK BUT LONG</stong>"_u8;
  case Raised:
    return "<stong>ALERT</stong>"_u8;
  case Unknown:
    ;
  }
  return "unknown"_u8;
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
      _status = Raised;
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
    for (auto dimensionValue: source->_children.keys()) {
      if (!_children.contains(dimensionValue)) {
        _children.insert(dimensionValue, new TreeItem(dimensionValue));
      }
      _children[dimensionValue]->merge(source->_children[dimensionValue]);
    }
    _status = source->_status;
    _timestamp = source->_timestamp;
  }
};

class DimensionData : public SharedUiItemDataBase<DimensionData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;

private:
  friend class Dimension;
  Utf8String _id;
  Utf8String _rawValue, _label;
  Utf8String id() const override { return _id; }
  QVariant uiData(int, int) const override { return {}; }
};

class Dimension : public SharedUiItem {
public:
  Dimension(PfNode node) {
    DimensionData *d = new DimensionData;
    QStringList idAndValue = node.contentAsTwoStringsList();
    d->_id = ConfigUtils::sanitizeId(
          idAndValue.value(0), ConfigUtils::AlphanumId).toUtf8();
    if (d->_id.isEmpty()) {
      delete d;
      return;
    }
    d->_rawValue = idAndValue.value(1);
    if (d->_rawValue.isEmpty())
      d->_rawValue = "%"+d->_id;
    d->_label = node.utf16attribute("label"_u8);
    setData(d);
  }
  PfNode toPfNode() const {
    const DimensionData *d = data();
    if (!d)
      return PfNode();
    QString idAndValue;
    idAndValue = d->_id+" "+d->_rawValue;
    PfNode node("dimension"_u8, idAndValue);
    if (!d->_label.isEmpty())
      node.setAttribute("label"_u8, d->_label);
    return node;
  }
  const DimensionData *data() const { return specializedData<DimensionData>(); }
  Utf8String rawValue() const {
    const DimensionData *d = data();
    return d ? d->_rawValue : Utf8String();
  }
  Utf8String label() const {
    const DimensionData *d = data();
    return d ? (d->_label.isEmpty() ? d->_id : d->_label) : Utf8String();
  }
};

} // unnamed namespace

Q_DECLARE_TYPEINFO(Dimension, Q_MOVABLE_TYPE);

static void mergeComponents(
    TreeItem *target, TreeItem *source,
    QList<QSharedPointer<TreeItem>> *components,
    QList<QHash<Utf8String,TreeItem*>> *indexes) {
  // recursive merge tree
  target->merge(source);
  // replace references in indexes
  for (int i = 0; i < indexes->size(); ++i) {
    QHash<Utf8String,TreeItem*> &index = (*indexes)[i];
    for (auto dimensionValue: index.keys())
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

class GridboardData : public SharedUiItemDataBase<GridboardData> {
public:
  static const Utf8String _qualifier;
  static const Utf8StringIndexedConstList _sectionNames;
  static const Utf8StringIndexedConstList _headerNames;
  Utf8String _id;
  Utf8String _label, _pattern, _info;
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
  QList<QHash<Utf8String,TreeItem*>> _dataIndexesByDimension;
  Utf8StringList _commentsList;
  qint64 _updatesCounter, _currentComponentsCount, _currentItemsCount;
  mutable QAtomicInteger<int> _rendersCounter;
  GridboardData() : _warningDelay(DEFAULT_WARNING_DELAY),
    _updatesCounter(0), _currentComponentsCount(0), _currentItemsCount(0) { }
  Utf8String id() const override { return _id; }
  QVariant uiData(int section, int role) const override;
};

Gridboard::Gridboard(PfNode node, Gridboard oldGridboard,
                     ParamSet parentParams) {
  Q_UNUSED(oldGridboard)
  GridboardData *d = new GridboardData;
  d->_id = node.contentAsUtf8();
  if (d->_id.isEmpty()) {
    Log::warning() << "gridboard with empty id: " << node.toString();
    delete d;
    return;
  }
  d->_label = node.utf16attribute("label"_u8);
  d->_info = node.utf16attribute("info"_u8);
  d->_pattern = node.utf16attribute("pattern"_u8);
  d->_patternRegexp = QRegularExpression(d->_pattern);
  if (!d->_patternRegexp.isValid())
    Log::warning() << "gridboard with invalid pattern: " << node.toString();
  for (const PfNode &child: node/"dimension") {
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
    d->_dataIndexesByDimension.append(QHash<Utf8String,TreeItem*>());
  d->_warningDelay = node["warningdelay"_u8]
                     .toDouble(DEFAULT_WARNING_DELAY/1e3)*1e3;
  d->_params.setParent(parentParams);
  d->_params = ParamSet(node, "param"_u8);
  ConfigUtils::loadComments(node, &d->_commentsList);
  // LATER load old state
  // LATER load initvalues
  setData(d);
}

void Gridboard::applyNewConfig(const Gridboard &other) {
  auto d = data();
  auto o = other.data();
  bool clear = false;
  if (o->_id != d->_id)
    return;
  d->_label = o->_label;
  d->_info = o->_info;
  if (d->_pattern != o->_pattern) {
    clear = true;
    d->_pattern = o->_pattern;
    d->_patternRegexp = o->_patternRegexp;
  }
  if (d->_dimensions != o->_dimensions) // ids are not the same in same order
    clear = true;
  d->_dimensions = o->_dimensions;
  d->_params = o->_params;
  d->_warningDelay = o->_warningDelay;
  //qDebug() << "apply new config" << d->_id << clear;
  if (clear)
    this->clear();
}

PfNode Gridboard::toPfNode() const {
  const GridboardData *d = data();
  if (!d)
    return PfNode();
  PfNode node("gridboard"_u8, d->_id);
  ConfigUtils::writeComments(&node, d->_commentsList);
  if (!d->_label.isEmpty() && d->_label != d->_id)
    node.setAttribute("label"_u8, d->_label);
  if (!d->_info.isEmpty())
    node.setAttribute("info"_u8, d->_info);
  node.setAttribute("pattern"_u8, d->_pattern);
  for (const Dimension &dimension: d->_dimensions)
    node.appendChild(dimension.toPfNode());
  // LATER initvalues
  ConfigUtils::writeParamSet(&node, d->_params, u"param"_s);
  if (d->_warningDelay != DEFAULT_WARNING_DELAY)
    node.setAttribute("warningdelay"_u8, d->_warningDelay/1e3);
  return node;
}

GridboardData *Gridboard::data() {
  return detachedData<GridboardData>();
}

const GridboardData *Gridboard::data() const {
  return specializedData<GridboardData>();
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
  auto rempp = RegexpParamsProvider(match);
  auto ppm = ParamsProviderMerger(&rempp)(&d->_params);
  QStringList dimensionValues;
  for (int i = 0; i < d->_dimensions.size(); ++i) {
    auto dimensionValue =
        PercentEvaluator::eval_utf8(d->_dimensions[i].rawValue(), &ppm);
    if (dimensionValue.isEmpty())
      dimensionValue = "(none)"_u8;
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

void Gridboard::detach() {
  SharedUiItem::detachedData<GridboardData>();
}

inline static Utf8String formatted(
    const Utf8String &text, const Utf8String &key, const ParamSet &params) {
  Utf8StringList slpp({text}); // provides %1
  return params.paramUtf8(key, text, &slpp);
}

QString Gridboard::toHtml() const {
  const GridboardData *d = data();
  if (!d)
    return QString("Null gridboard");
  d->_rendersCounter.fetchAndAddOrdered(1);
  if (d->_dataComponents.isEmpty())
    return QString("No data so far");
  auto tableClass = d->_params.paramUtf8("gridboard.tableclass",
                                         "table table-condensed table-hover");
  auto divClass = d->_params.paramUtf8("gridboard.divclass",
                                       "row gridboard-status");
  auto componentClass = d->_params.paramUtf8("gridboard.componentclass",
                                             "gridboard-component");
  auto tdClassOk = d->_params.paramUtf8("gridboard.tdclass.ok");
  auto tdClassWarning = d->_params.paramUtf8("gridboard.tdclass.warning",
                                             "warning");
  auto tdClassAlerted = d->_params.paramUtf8("gridboard.tdclass.alerted",
                                             "danger");
  auto tdClassUnknown = d->_params.paramUtf8("gridboard.tdclass.unknown");
  Utf8String s;
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
  for (const QSharedPointer<TreeItem> &componentRoot: componentRoots) {
    QDateTime now = QDateTime::currentDateTime();
    QHash<Utf8String,QHash<Utf8String,const TreeItem*>> matrix;
    Utf8StringList rows, columns;
    switch (d->_dimensions.size()) {
    case 0:
      return "Gridboard with 0 dimensions.";
    case 2: {
      Utf8StringSet columnsSet;
      for (const TreeItem *treeItem1: componentRoot->_children) {
        rows.append(treeItem1->_name);
        for (const TreeItem *treeItem2: treeItem1->_children) {
          matrix[treeItem1->_name][treeItem2->_name] = treeItem2;
          columnsSet.insert(treeItem2->_name);
        }
      }
      columns = columnsSet.toSortedList();
      break;
    }
    default:
      // LATER support more than 2 dimensions
      return "Gridboard do not yet support more than 2 dimensions.";
    }
    s = s+"<div class=\""+componentClass+"\"><table class=\""+tableClass
        +"\" id=\"gridboard."+d->_id+"\"><tr><th>&nbsp;</th>";
    for (auto column: columns)
      s = s+"<th>"
          +formatted(column, "gridboard.columnformat", d->_params)+"</th>";
    for (auto row: rows) {
      s = s+"</tr><tr><th>"
          +formatted(row, "gridboard.rowformat", d->_params)+"</th>";
      for (auto column: columns) {
        const TreeItem *item = matrix[row][column];
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
        case Raised:
          tdClass = tdClassAlerted;
          break;
        case Unknown:
          tdClass = tdClassUnknown;
          break;
        }
        s = s+"<td class=\""+tdClass+"\">"
            +(item ? statusToHtmlHumanReadableString(status)
                     +"<br/>"+Utf8String(
                       item->_timestamp.toString(u"yyyy-MM-dd hh:mm:ss,zzz"_s))
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
      Utf8StringList list;
      for (auto dimension: _dimensions) {
        list << dimension.id() + "=" + dimension.rawValue();
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

const Utf8String DimensionData::_qualifier = "gridboarddimension";

const Utf8StringIndexedConstList DimensionData::_sectionNames;

const Utf8StringIndexedConstList DimensionData::_headerNames;

const Utf8String GridboardData::_qualifier = "gridboard";

const Utf8StringIndexedConstList GridboardData::_sectionNames {
  "id", // 0
  "label",
  "pattern",
  "dimensions",
  "params",
  "warning_delay", // 5
  "updates_count",
  "renders_count",
  "components_count",
  "items_count",
  "additional_info" // 10
};

const Utf8StringIndexedConstList GridboardData::_headerNames {
  "Id", // 0
  "Label",
  "Pattern",
  "Dimensions",
  "Params",
  "Warning Delay", // 5
  "Updates Count",
  "Renders Count",
  "Components Count",
  "Items Count",
  "Additional Info" // 10
};
