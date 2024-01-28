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
#include "requestformfield.h"
#include "sched/taskinstance.h"
#include "configutils.h"

static QStringList _allowedValuesCsvHeaders { "value", "label", "flags" };

class RequestFormFieldData : public QSharedData {
public:
  QString _id, _label, _placeholder, _suggestion;
  QRegularExpression _format;
  QList<QStringList> _allowedValues;
  QString _allowedValuesSource;
  bool _mandatory;
  RequestFormFieldData() : _mandatory(false) { }
};

RequestFormField::RequestFormField() : d(new RequestFormFieldData) {
}

RequestFormField::RequestFormField(const RequestFormField &rhs) : d(rhs.d) {
}

RequestFormField::RequestFormField(PfNode node) {
  RequestFormFieldData *d = new RequestFormFieldData;
  QString id = node.contentAsUtf16();
  if (id.isEmpty()) {
    Log::error() << "request form field without id "
                 << node.toString();
    return;
  }
  d->_id = ConfigUtils::sanitizeId(id, ConfigUtils::LocalId);
  d->_label = node.utf16attribute("label", d->_id);
  d->_placeholder = node.utf16attribute("placeholder", d->_label);
  d->_suggestion = node.utf16attribute("suggestion");
  d->_mandatory = node.hasChild("mandatory");
  auto [child,unwanted] = node.first_two_children("allowedvalues");
  if (!!child) {
    if (child.isArray()) {
      d->_allowedValues = child.contentAsArray().rows();
    } else {
      d->_allowedValuesSource = child.contentAsUtf16();
    }
  }
  if (!!unwanted) {
    Log::error() << "request form field with several allowedvalues: "
                 << node.toString();
  }
  // LATER remove duplicates in allowedvalues ?
  QString format = node.utf16attribute("format");
  if (!d->_allowedValues.isEmpty() || !d->_allowedValuesSource.isEmpty()) {
    // allowedvalues is set, therefore format must be generated from allowed
    // values, and ignored if found in config
    if (!format.isEmpty()) {
      Log::warning() << "request form field with both allowedvalues and "
                        "format, ignoring format"
                     << node.toString();
    }
    if (!d->_allowedValues.isEmpty()) {
      format = "(?:";
      bool first = true;
      for (const QStringList &row: d->_allowedValues) {
        if (row.isEmpty())
          continue; // should never happen
        if (first)
          first = false;
        else
          format.append('|');
        format.append(QRegularExpression::escape(row[0]));
      }
      format.append(')');
    } else {
      // LATER check allowed values when dynamic
      format = QString();
    }
  }
  if (!format.isEmpty()) {
    if (!format.startsWith('^'))
      format.prepend('^');
    if (!format.endsWith('$'))
      format.append('$');
    d->_format = QRegularExpression(format);
    if (!d->_format.isValid()) {
      Log::error() << "request form field with invalid format specification: "
                   << d->_format.errorString() << " : " << node.toString();
      return;
    }
  }
  this->d = d;
  //Log::fatal() << "RequestFormField: " << node.toString() << " " << toHtml();
}

RequestFormField::~RequestFormField() {
}

RequestFormField &RequestFormField::operator=(const RequestFormField &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

QString RequestFormField::toHtmlFormFragment(
    ReadOnlyResourcesCache *resourcesCache, bool *errorOccured) const {
  QString html;
  if (!d)
    return html;
  // header and label
  html = "<div class=\"from-group\">\n"
         "  <label class=\"control-label\" for=\""+d->_id+"\">"+d->_label
         +"</label>";
  if (d->_mandatory)
    html.append(" <small>(mandatory)</small>");
  html.append("\n");
  if (!d->_allowedValues.isEmpty() || !d->_allowedValuesSource.isEmpty()) {
    // combobox field
    bool hasDefault = false;
    QString options;
    QList<QStringList> rows = d->_allowedValues;
    if (!d->_allowedValuesSource.isEmpty()) {
      QString errorString;
      QByteArray data = resourcesCache->fetchResource(d->_allowedValuesSource,
                                                      &errorString);
      if (!data.isEmpty()) {
        CsvFile csvfile;
        csvfile.setFieldSeparator(';'); // consistent with pf data
        csvfile.openReadonly(data);
        rows = csvfile.rows();
      } else {
        Log::warning() << "cannot fetch request form field allowed values list "
                          "from " << d->_allowedValuesSource << " : "
                       << errorString;
        html.append("<p class=\"bg-warning\"><i class=\"icon-warning\"></i> "
                    "error: could not fetch allowed values list");
        options = "<option value=\"\" selected>error: could not fetch allowed "
                  "values list</option>";
        hasDefault = true;
        if (errorOccured)
          *errorOccured = true;
        rows.clear();
      }
    }
    for (const QStringList &row: rows) {
      options.append("<option");
      QString value = row.value(0);
      QString label = row.value(1);
      options.append(" value=\"")
          .append(StringUtils::htmlEncode(value, false, false))
          .append("\"");
      QString s;
      if (value == d->_suggestion || row.value(2).contains('d')) {
        options.append(" selected");
        hasDefault = true;
      }
      options.append(">");
      options.append(StringUtils::htmlEncode(label.isEmpty() ? value : label,
                                             false, false))
          .append("</option>");
    }
    if (!d->_mandatory) {
      // add an empty string option to allow empty == null value
      if (!hasDefault)
        options.append("<option selected></option>");
      else
        options.append("<option></option>");
    }
    html.append(
          "  <div>\n"
          "    <select id=\""+d->_id+"\" name=\""+d->_id+"\" value=\""
          +d->_suggestion+"\" class=\"form-control\">\n"
          "      "+options+"\n"
          "    </select>\n"
          "  </div>\n");
  } else {
    // text field
    html.append(
          "  <div>\n"
          "    <input id=\""+d->_id+"\" name=\""+d->_id+"\" type=\"text\" "
          "placeholder=\""+d->_placeholder+"\" value=\""+d->_suggestion+"\""
          "class=\"form-control\""+(d->_mandatory?"required":"")+">\n"
          "  </div>\n");
  }
  html.append("  </div>\n");
  return html;
}

QString RequestFormField::toHtmlHumanReadableDescription() const {
  QString v;
  if (d) {
    v = "<p>"+StringUtils::htmlEncode(d->_id)+":<dl class=\"dl-horizontal\">";
    if (!d->_label.isEmpty())
      v.append("<dt>label</dt><dd>").append(StringUtils::htmlEncode(d->_label))
          .append("</dd>");
    if (!d->_suggestion.isEmpty())
      v.append("<dt>suggestion</dt><dd>")
          .append(StringUtils::htmlEncode(d->_suggestion)).append("</dd>");
    if (!d->_mandatory)
      v.append("<dt>mandatory</dt><dd>true</dd>");
    if (!d->_placeholder.isEmpty())
      v.append("<dt>placeholder</dt><dd>")
          .append(StringUtils::htmlEncode(d->_placeholder)).append("</dd>");
    if (!d->_allowedValues.isEmpty()) {
      v.append("<dt>allowed values</dt><dd>")
          .append(StringUtils::htmlEncode(
                    StringUtils::columnFromRows(d->_allowedValues, 0)
                    .join(' ')))
          .append("</dd>");
    }
    if (!d->_allowedValuesSource.isEmpty()) {
      v.append("<dt>allowed values source</dt><dd>")
          .append(StringUtils::htmlEncode(d->_allowedValuesSource))
          .append("</dd>");
    }
    if (d->_format.isValid())
      v.append("<dt>format</dt><dd>")
          .append(StringUtils::htmlEncode(d->_format.pattern()))
          .append("</dd>");
    v.append("</dl>");
  }
  return v;
}

bool RequestFormField::validate(QString value) const {
  return d && d->_format.match(value).hasMatch();
}

bool RequestFormField::isNull() const {
  return !d;
}

QString RequestFormField::id() const {
  return d ? d->_id : QString();
}

QString RequestFormField::format() const {
  return d ? d->_format.pattern() : QString();
}

PfNode RequestFormField::toPfNode() const {
  if (!d)
    return PfNode();
  PfNode node("field", d->_id);
  if (d->_label != d->_id)
    node.appendChild(PfNode("label", d->_label));
  if (d->_placeholder != d->_label)
    node.appendChild(PfNode("placeholder", d->_placeholder));
  if (!d->_suggestion.isEmpty())
    node.appendChild(PfNode("suggestion", d->_suggestion));
  if (d->_mandatory)
    node.appendChild(PfNode("mandatory"));
  if (!d->_allowedValues.isEmpty())
    node.appendChild(PfNode("allowedvalues",
                            PfArray(_allowedValuesCsvHeaders,
                                    d->_allowedValues)));
  else if (!d->_allowedValuesSource.isEmpty())
    node.setAttribute("allowedvalues", d->_allowedValuesSource);
  else if (d->_format.isValid() && !d->_format.pattern().isEmpty())
    node.appendChild(PfNode("format", d->_format.pattern()));
  return node;
}
