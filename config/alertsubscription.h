/* Copyright 2012-2023 Hallowyn and others.
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
#ifndef ALERTSUBSCRIPTION_H
#define ALERTSUBSCRIPTION_H

#include "libqron_global.h"
#include "modelview/shareduiitem.h"

class AlertSubscriptionData;
class PfNode;
class Alert;

/** Alert subscription is the configuration object defining the matching between
 * an alert id pattern and a alert channel, with optional additional parameters.
 * @see Alerter */
class LIBQRONSHARED_EXPORT AlertSubscription : public SharedUiItem {
public:
  AlertSubscription();
  AlertSubscription(const AlertSubscription &other);
  /** parses this form in Pf:
   * (subscription
   *  (param foo bar)
   *  (match **)
   *  (log
   *   (param baz baz)
   *   (address debug)
   *   (emitmessage foo)
   *  )
   * )
   * Where subscriptionnode is "subscription" node and channelnode is "log"
   * (or any other channel name) node.
   * This is needed because input config format accepts several channel nodes
   * per subscription node, in this case config reader will create several
   * AlertSubscription objects.
   */
  AlertSubscription(PfNode subscriptionnode, PfNode channelnode,
                    ParamSet parentParams);
  AlertSubscription &operator=(const AlertSubscription &other) {
    SharedUiItem::operator=(other); return *this; }
  QString pattern() const;
  QRegularExpression patternRegexp() const;
  Utf8String channelName() const;
  Utf8String rawAddress() const;
  Utf8String address(Alert alert) const;
  Utf8String emitMessage(Alert alert) const;
  Utf8String cancelMessage(Alert alert) const;
  Utf8String reminderMessage(Alert alert) const;
  /** human readable description such as "emitmessage=foo remindermessage=bar"
   * or "" or "cancelmessage=baz" */
  Utf8String messagesDescriptions() const;
  bool notifyEmit() const;
  bool notifyCancel() const;
  bool notifyReminder() const;
  ParamSet params() const;
  PfNode toPfNode() const;

private:
  inline const AlertSubscriptionData *data() const;
};

Q_DECLARE_METATYPE(AlertSubscription)
Q_DECLARE_TYPEINFO(AlertSubscription, Q_MOVABLE_TYPE);

#endif // ALERTSUBSCRIPTION_H
