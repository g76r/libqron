/* Copyright 2013-2025 Hallowyn and others.
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
#include "format/stringutils.h"
#include "log/log.h"
#include "csv/csvfile.h"

class RequestFormFieldData : public QSharedData {
public:
  QString _id, _label, _placeholder, _suggestion;
  QRegularExpression _format, _cause;
};

RequestFormField::RequestFormField() : d(new RequestFormFieldData) {
}

RequestFormField::RequestFormField(const RequestFormField &rhs) : d(rhs.d) {
}

RequestFormField::RequestFormField(const PfNode &node) {
  RequestFormFieldData *d = new RequestFormFieldData;
  QString id = node.content_as_text();
  if (id.isEmpty()) {
    Log::error() << "request form field without id " << node.as_text();
    return;
  }
  d->_id = ConfigUtils::sanitizeId(id, ConfigUtils::LocalId);
  d->_label = node.attribute("label", d->_id);
  d->_placeholder = node.attribute("placeholder", d->_label);
  d->_suggestion = node.attribute("suggestion");
  auto format = node.attribute("format");
  if (!format.isEmpty()) {
    if (!format.startsWith('^'))
      format.prepend('^');
    if (!format.endsWith('$'))
      format.append('$');
    d->_format = QRegularExpression(format);
    if (!d->_format.isValid()) {
      Log::error() << "request form field with invalid format specification: "
                   << d->_format.errorString() << " : " << node.as_text();
      return;
    }
    d->_cause =
        QRegularExpression(node.first_child("format")
                           .attribute("cause","^api|notice"));
  }
  this->d = d;
}

RequestFormField::~RequestFormField() {
}

RequestFormField &RequestFormField::operator=(const RequestFormField &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

QString RequestFormField::toHtmlFormFragment(
    ReadOnlyResourcesCache *, bool *) const {
  QString html;
  if (!d)
    return html;
  auto has_pattern = !d->_format.pattern().isEmpty();
  auto required = has_pattern && !d->_format.match(u""_s).hasMatch();
  // header and label
  html = "<div class=\"from-group\">\n"
         "  <label class=\"control-label\" for=\""+d->_id+"\">"+d->_label+
         (required ? "&nbsp;<i class=\"fa-solid fa-star-of-life\"></i>" : "")+
         "</label>";
  html.append("\n");
  // text field
  html.append(
        "  <div>\n"
        "    <input id=\""+d->_id+"\" name=\""+d->_id+
        "\" type=\"text\" placeholder=\""+d->_placeholder+
        "\" value=\""+d->_suggestion+"\""+
        (has_pattern ? " pattern=\""+d->_format.pattern()+"\"" : "")+
        (required ? " required" : "")+
        " class=\"form-control\">\n"
        "  </div>\n");
  html.append("</div>\n");
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
    if (!d->_placeholder.isEmpty())
      v.append("<dt>placeholder</dt><dd>")
          .append(StringUtils::htmlEncode(d->_placeholder)).append("</dd>");
    if (!d->_format.pattern().isEmpty())
      v = v+"<dt>format</dt><dd>"+StringUtils::htmlEncode(d->_format.pattern())+
          " with cause filter "+StringUtils::htmlEncode(d->_cause.pattern())+
          "</dd>";
    v.append("</dl>");
  }
  return v;
}

bool RequestFormField::validate(
    const Utf8String &value, const Utf8String &cause) const {
  if (!d) [[unlikely]] // should never happen
    return false;
  if (!d->_cause.match(cause).hasMatch()) // no format or invalid cause filter
    return true;
  return d->_format.match(value).hasMatch();
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
    node.append_child({"label", d->_label});
  if (d->_placeholder != d->_label)
    node.append_child({"placeholder", d->_placeholder});
  if (!d->_suggestion.isEmpty())
    node.append_child({"suggestion", d->_suggestion});
  if (!d->_format.pattern().isEmpty() && !d->_format.pattern().isEmpty())
    node.append_child({"format", d->_format.pattern(), {
                         {"cause",d->_cause.pattern()},
                       }
                      });
  return node;
}
