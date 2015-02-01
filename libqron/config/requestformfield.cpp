/* Copyright 2013-2014 Hallowyn and others.
 * This file is part of qron, see <http://qron.hallowyn.com/>.
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
#include <QSharedData>
#include <QRegExp>
#include <QPair>
#include "pf/pfnode.h"
#include "log/log.h"
#include "sched/taskinstance.h"
#include "configutils.h"
#include "util/htmlutils.h"

class RequestFormFieldData : public QSharedData {
public:
  QString _id, _label, _placeholder, _suggestion;
  QRegExp _format;
  QString _appendcommand;
  ParamSet _setenv;
};

RequestFormField::RequestFormField() : d(new RequestFormFieldData) {
}

RequestFormField::RequestFormField(const RequestFormField &rhs) : d(rhs.d) {
}

RequestFormField::RequestFormField(PfNode node) {
  RequestFormFieldData *d = new RequestFormFieldData;
  QString id = node.contentAsString();
  if (id.isEmpty()) {
    Log::error() << "request form field without id "
                 << node.toString();
    return;
  }
  d->_id = ConfigUtils::sanitizeId(id, ConfigUtils::TaskId);
  d->_label = node.attribute("label", d->_id);
  d->_placeholder = node.attribute("placeholder", d->_label);
  d->_suggestion = node.attribute("suggestion");
  QString format = node.attribute("format");
  d->_format = QRegExp(format);
  if (!d->_format.isValid()) {
    Log::error() << "request form field with invalid format specification: "
                 << node.toString();
    return;
  }
  d->_appendcommand = node.attribute("appendcommand");
  ConfigUtils::loadParamSet(node, &d->_setenv, "setenv");
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

QString RequestFormField::toHtmlFormFragment(QString inputClass) const {
  if (!d)
    return QString();
  return "<div class=\"control-group\">\n"
      "  <label class=\"control-label\" for=\""+d->_id+"\">"+d->_label
      +"</label>\n"
      "  <div class=\"controls\">\n"
      "    <input id=\""+d->_id+"\" name=\""+d->_id+"\" type=\"text\" "
      "placeholder=\""+d->_placeholder+"\" value=\""+d->_suggestion+"\""
      "class=\""+inputClass+"\">\n"
      "  </div>\n"
      "</div>\n";
}

QString RequestFormField::toHtmlHumanReadableDescription() const {
  QString v;
  if (d) {
    v = "<p>"+HtmlUtils::htmlEncode(d->_id)+":<dl class=\"dl-horizontal\">";
    if (!d->_label.isEmpty())
      v.append("<dt>label</dt><dd>").append(HtmlUtils::htmlEncode(d->_label))
          .append("</dd>");
    if (!d->_suggestion.isEmpty())
      v.append("<dt>suggestion</dt><dd>")
          .append(HtmlUtils::htmlEncode(d->_suggestion)).append("</dd>");
    if (!d->_placeholder.isEmpty())
      v.append("<dt>placeholder</dt><dd>")
          .append(HtmlUtils::htmlEncode(d->_placeholder)).append("</dd>");
    if (d->_format.isValid())
      v.append("<dt>format</dt><dd>")
          .append(HtmlUtils::htmlEncode(d->_format.pattern())).append("</dd>");
    if (!d->_setenv.isEmpty()) {
      v.append("<dt>setenv</dt><dd>")
          .append(HtmlUtils::htmlEncode(d->_setenv.toString()))
          .append("</dd>");
    }
    if (!d->_appendcommand.isEmpty())
      v.append("<dt>appendcommand</dt><dd>")
          .append(HtmlUtils::htmlEncode(d->_appendcommand)).append("</dd>");
    v.append("</dl>");
  }
  return v;
}

bool RequestFormField::validate(QString value) const {
  return d && d->_format.exactMatch(value);
}

void RequestFormField::apply(QString value, TaskInstance *request) const {
  if (request && d && !value.isNull()) {
    request->overrideParam(d->_id, value);
    QString command(request->command());
    if (!d->_appendcommand.isEmpty())
      command.append(' ').append(d->_appendcommand);
    request->overrideCommand(command);
    foreach (const QString &key, d->_setenv.keys())
      request->overrideSetenv(key, d->_setenv.value(key));
  }
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
  if (!d->_format.isEmpty() && d->_format.isValid())
    node.appendChild(PfNode("format", d->_format.pattern()));
  if (!d->_appendcommand.isEmpty())
    node.appendChild(PfNode("appendcommand", d->_appendcommand));
  ConfigUtils::writeParamSet(&node, d->_setenv, "setenv");
  return node;
}
