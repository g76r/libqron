/* Copyright 2013 Hallowyn and others.
 * This file is part of libqtssu, see <https://github.com/g76r/libqtssu>.
 * Libqtssu is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Libqtssu is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public License
 * along with libqtssu.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HTMLALERTITEMDELEGATE_H
#define HTMLALERTITEMDELEGATE_H

#include "textview/htmlitemdelegate.h"

/** Specific item delegate for alerts (both raised alerts and emited alerts). */
class HtmlAlertItemDelegate : public HtmlItemDelegate {
  Q_OBJECT
  Q_DISABLE_COPY(HtmlAlertItemDelegate)
  int _alertColumn, _actionsColumn;
  bool _canCancel;

public:
  explicit HtmlAlertItemDelegate(QObject *parent, int alertColumn,
                                 int actionsColumn, bool canCancel);
  QString text(const QModelIndex &index) const;
};

#endif // HTMLALERTITEMDELEGATE_H
