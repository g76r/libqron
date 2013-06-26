/* Copyright 2013 Hallowyn and others.
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
#ifndef ALERTCHANNELSMODEL_H
#define ALERTCHANNELSMODEL_H

#include "textview/textmatrixmodel.h"
#include <QStringList>

/** Model holding alert channels list, one per line. Currently almost
 * useless. */
class AlertChannelsModel : public TextMatrixModel {
  Q_OBJECT
  Q_DISABLE_COPY(AlertChannelsModel)

public:
  explicit AlertChannelsModel(QObject *parent = 0);
  
public slots:
  void channelsChanged(QStringList channels);
};

#endif // ALERTCHANNELSMODEL_H
