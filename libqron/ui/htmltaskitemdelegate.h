/* Copyright 2013-2014 Hallowyn and others.
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
#ifndef HTMLTASKITEMDELEGATE_H
#define HTMLTASKITEMDELEGATE_H

#include "libqron_global.h"
#include "textview/htmlitemdelegate.h"

/** Specific item delegate for tasks. */
class LIBQRONSHARED_EXPORT HtmlTaskItemDelegate : public HtmlItemDelegate {
  Q_OBJECT
  Q_DISABLE_COPY(HtmlTaskItemDelegate)
public:
  explicit HtmlTaskItemDelegate(QObject *parent = 0);
  QString text(const QModelIndex &index) const;
  QString headerText(int section, Qt::Orientation orientation,
                     const QAbstractItemModel *model) const;
};

#endif // HTMLTASKITEMDELEGATE_H
