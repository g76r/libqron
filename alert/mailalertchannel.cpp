/* Copyright 2012-2025 Hallowyn and others.
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
#include "mailalertchannel.h"
#include "mail/mailsender.h"
#include "alerter.h"
#include "mail/mailaddress.h"
#include <QTimer>
#include <QThread>

// LATER replace this 60" ugly batch with _remindFrequency and make reminding no longer drift
#define ASYNC_PROCESSING_INTERVAL 60000
// LATER parametrize retry delay, set a maximum data retention, etc.
#define RETRY_INTERVAL 60000

class MailAlertQueue {
public:
  QString _address;
  QList<Alert> _alerts, _cancellations;
  QSet<Alert> _reminders;
  QDateTime _lastMail, _nextProcessing;
  MailAlertQueue(const QString address = QString()) : _address(address) { }
  void scheduleNext(MailAlertChannel *channel, qint64 ms) {
    QDateTime scheduling = QDateTime::currentDateTime().addMSecs(ms);
    _nextProcessing = scheduling;
    QTimer::singleShot(ms, Qt::PreciseTimer, channel, [this,channel](){
      channel->processQueue(_address);
    });
  }
  QString toString() const {
    QString s = "{\n  address: "+_address+"\n  alerts: [ ";
    for (const Alert &alert: _alerts)
      s.append(alert.id()).append(' ');
    s.append("]\n  cancellations: [ ");
    for (const Alert &alert: _cancellations)
      s.append(alert.id()).append(' ');
    s.append("]\n  reminders: [ ");
    for (const Alert &alert: _reminders)
      s.append(alert.id()).append(' ');
    s.append("]\n  last mail: ").append(_lastMail.toString())
        .append("\n  next processing: ").append(_nextProcessing.toString());
    s.append("\n}");
    return s;
  }
};

MailAlertChannel::MailAlertChannel(Alerter *alerter)
  : AlertChannel(alerter), _mailSender(0),
    _asyncProcessingTimer(new QTimer) {
  _thread->setObjectName("MailAlertChannelThread");
  connect(_asyncProcessingTimer, &QTimer::timeout,
          this, &MailAlertChannel::asyncProcessing);
  // TODO have QTimer be a child of this
  connect(this, &MailAlertChannel::destroyed,
          _asyncProcessingTimer, &QTimer::deleteLater);
  _asyncProcessingTimer->start(ASYNC_PROCESSING_INTERVAL);
}

void MailAlertChannel::setConfig(AlerterConfig config) {
  _config = config;
  // LATER make server specification more user friendly, e.g. "localhost:25" or "localhost"
  QString relay = _config.params().paramUtf16("mail.relay", "smtp://127.0.0.1");
  int smtpTimeoutMs = std::max(
        _config.params().paramNumber<double>("mail.smtp.timeout", 5)*1000, 1.0);
  if (_mailSender)
    delete _mailSender;
  _mailSender = new MailSender(relay, smtpTimeoutMs);
  QString queuesBeforeReload;
  QStringList queuesRemoved;
  QSet<QString> configuredAddresses;
  for (const AlertSubscription &subscription: _config.alertSubscriptions())
    if (subscription.channelName() == "mail")
      for (const QString &address:
               splittedAddresses(subscription.address(Alert())))
        configuredAddresses.insert(address);
  QDateTime maxLastMailDate = // date until when an empty queue can be removed
      QDateTime::currentDateTime().addMSecs(-_config.remindPeriod());
  for (const MailAlertQueue *queue: _queues) {
    queuesBeforeReload.append(queue->toString()).append("\n");
    if ((queue->_alerts.size() + queue->_cancellations.size()
         + queue->_reminders.size() == 0
         && queue->_lastMail < maxLastMailDate)
        || !configuredAddresses.contains(queue->_address)) {
      _queues.remove(queue->_address);
      queuesRemoved.append(queue->_address);
      delete queue;
    }
  }
  Log::debug() << "mail queues before config reload: [\n" << queuesBeforeReload
               << "]";
  Log::debug() << "mail addresses with configured subscription:"
               << configuredAddresses.values();
  Log::info() << "mail queues removed on reload: [ "
              << queuesRemoved.join(' ') << " ]";
  QMetaObject::invokeMethod(this, [this](){
    asyncProcessing();
  }, Qt::QueuedConnection);
  Log::debug() << "MailAlertChannel configured " << relay << " "
               << config.params().toString();
}

MailAlertChannel::~MailAlertChannel() {
  if (_mailSender)
    delete _mailSender;
  for (MailAlertQueue *queue: _queues)
    delete queue;
}

QStringList MailAlertChannel::splittedAddresses(
    QString commaSeparatedAddresses) {
  QStringList addresses;
  static const QRegularExpression _whitespace{ "\\s+" };
  for (const auto &s: commaSeparatedAddresses.split(_whitespace)) {
    MailAddress addr(s);
    if (!!addr)
      addresses.append(s);
    else
      Log::error() << "MailAlertChannel ignoring invalid recipient email "
                      "address: " << s;
  }
  return addresses;
}

void MailAlertChannel::doNotifyAlert(Alert alert) {
  for (QString address:
       splittedAddresses(alert.subscription().address(alert))) {
    address = address.trimmed();
    MailAlertQueue *queue = _queues.value(address);
    if (!queue) {
      queue = new MailAlertQueue(address); // LATER garbage collect queues
      _queues.insert(address, queue);
    }
    switch (alert.status()) {
    case Alert::Raised:
      if (alert.subscription().notifyReminder()) {
        alert.setLastRemindedDate(QDateTime());
        queue->_reminders.insert(alert);
      }
      Q_FALLTHROUGH();
    case Alert::Nonexistent:
      if (!alert.subscription().notifyEmit())
        return;
      queue->_alerts.append(alert);
      break;
    case Alert::Canceled:
      queue->_reminders.remove(alert);
      if (!alert.subscription().notifyCancel())
        return;
      queue->_cancellations.append(alert);
      break;
    case Alert::Rising:
    case Alert::MayRise:
    case Alert::Dropping:
      ; // should never happen
    };
    if (queue->_nextProcessing.isNull()) {
      // wait for a while before sending a mail with only 1 alert, in case some
      // related alerts are coming soon after this one
      queue->scheduleNext(this, _config.delayBeforeFirstSend());
    }
  }
}

void MailAlertChannel::processQueue(QVariant address) {
  const QString addr(address.toString());
  MailAlertQueue *queue = _queues.value(addr);
  if (!queue) { // should never happen
    Log::debug() << "MailAlertChannel::processQueue called for an address with "
                    "no queue: " << address;
    return;
  }
  QDateTime now = QDateTime::currentDateTime();
  queue->_nextProcessing = QDateTime();
  bool haveJob = false;
  if (queue->_alerts.size() || queue->_cancellations.size())
    haveJob = true;
  else {
    for (const Alert &alert: queue->_reminders) {
      QDateTime dt = alert.lastRemindedDate();
      if (dt.isValid() && dt.msecsTo(now) > _config.remindPeriod()) {
        haveJob = true;
        break;
      }
    }
  }
  if (haveJob) {
    QList<Alert> reminders;
    for (Alert alert: queue->_reminders) {
      if (!queue->_alerts.contains(alert)) // ignore alerts also reported as new ones
        reminders.append(alert);
      alert.setLastRemindedDate(now);
      queue->_reminders.insert(alert); // update all timestamps
    }
    std::sort(reminders.begin(), reminders.end());
    QString errorString;
    int ms = queue->_lastMail.msecsTo(QDateTime::currentDateTime());
    int minDelayBetweenSend = _config.minDelayBetweenSend();
    //Log::fatal() << "minDelayBetweenSend: " << minDelayBetweenSend << " / "
    //             << ms;
    if (queue->_lastMail.isNull() || ms >= minDelayBetweenSend) {
      //Log::debug() << "MailAlertChannel::processQueue trying to send alerts "
      //                "mail to " << addr << ": " << queue->_alerts.size()
      //             << " new alerts + " << queue->_cancellations.size()
      //             << " cancellations + " << reminders.size()
      //             << " reminders";
      QStringList recipients(addr);
      QString text, subject, boundary("THISISTHEMIMEBOUNDARY"), s;
      QString html;
      QHash<QString,QString> headers;
      // headers
      if (queue->_alerts.size())
        subject = _config.params().paramUtf16(
                    "mail.alertsubject."+queue->_address,
                    _config.params().paramUtf16(
                      "mail.alertsubject", "NEW QRON ALERT"));
      else if (queue->_cancellations.size())
        subject = _config.params().paramUtf16(
                    "mail.cancelsubject."+queue->_address,
                    _config.params().paramUtf16(
                      "mail.cancelsubject", "canceling qron alert"));
      else
        subject = _config.params().paramUtf16(
                    "mail.remindersubject."+queue->_address,
                    _config.params().paramUtf16(
                      "mail.remindersubject", "qron alert reminder"));
      headers.insert("Subject", subject);
      headers.insert("To", addr);
      headers.insert("User-Agent", "qron free scheduler (www.qron.eu)");
      headers.insert("X-qron-previous-mail", queue->_lastMail.isNull()
                     ? "none" : queue->_lastMail.toString(Qt::ISODate));
      headers.insert("X-qron-alerts-count",
                     QString::number(queue->_alerts.size()));
      headers.insert("X-qron-cancellations-count",
                     QString::number(queue->_cancellations.size()));
      headers.insert("X-qron-reminders-count",
                     QString::number(reminders.size()));
      // body
      html = "<html><head><title>"+subject+"</title></head><body>";
      QString webConsoleUrl = _config.params().paramUtf16(
                                "webconsoleurl."+queue->_address,
                                _config.params().paramUtf16("webconsoleurl"));
      if (!webConsoleUrl.isEmpty()) {
        text.append("Alerts can also be viewed here:\r\n")
            .append(webConsoleUrl).append("\r\n\r\n");
        html.append("<p>Alerts can also be viewed here: <a href=\"")
            .append(webConsoleUrl).append("\">").append(webConsoleUrl)
            .append("</a>.</p>\n");
      }
      s = "This message contains ";
      s.append(QString::number(queue->_alerts.size()))
          .append(" new emited alerts, ")
          .append(QString::number(queue->_cancellations.size()))
          .append(" alert cancellations and ")
          .append(QString::number(reminders.size()))
          .append(" reminders.");
      text.append(s).append("\r\n\r\n");
      html.append("<p>").append(s).append("</p>\n");
      text.append("NEW EMITED ALERTS:\r\n\r\n");
      html.append("<p>NEW EMITED ALERTS:</p>\n<ul>\n");
      if (queue->_alerts.isEmpty()) {
        text.append("(none)\r\n");
        html.append("<li>(none)</li>\n");
      } else {
        for (const Alert &alert: queue->_alerts) {
          s = alert.riseDate().toString("yyyy-MM-dd hh:mm:ss,zzz");
          s.append(" ").append(alert.subscription().emitMessage(alert))
              .append("\r\n");
          text.append(s);
          QString style =
              _config.params().paramUtf16(
                "mail.alertstyle."+queue->_address,
                _config.params().paramUtf16(
                  "mail.alertstyle", "background:#ff0000;color:#ffffff;"));
          html.append("<li style=\"").append(style).append("\">")
              .append(s).append("</li>");
        }
      }
      text.append("\r\nFormer alerts canceled:\r\n\r\n");
      html.append("</ul>\n<p>Former alerts canceled:</p>\n<ul>\n");
      if (queue->_cancellations.isEmpty()) {
        text.append("(none)\r\n");
        html.append("<li>(none)</li>\n");
      } else {
        for (const Alert &alert: queue->_cancellations) {
          s = alert.cancellationDate().toString("yyyy-MM-dd hh:mm:ss,zzz");
          s.append(" ").append(alert.subscription().cancelMessage(alert))
              .append(" (raised on ").append(
                alert.riseDate().toString("yyyy-MM-dd hh:mm:ss,zzz"))
              .append(")\r\n");
          text.append(s);
          QString style =
              _config.params().paramUtf16(
                "mail.cancelstyle."+queue->_address,
                _config.params().paramUtf16(
                  "mail.cancelstyle", "background:#8080ff"));
          html.append("<li style=\"").append(style).append("\">")
              .append(s).append("</li>");
        }
      }
      text.append("\r\nAlerts reminders (alerts still raised):\r\n\r\n");
      html.append("</ul>\n<p>Alerts reminders (alerts still raised):</p>\n<ul>\n");
      if (reminders.isEmpty()) {
        text.append("(none)\r\n");
        html.append("<li>(none)</li>\n");
      } else {
        for (const Alert &alert: reminders) {
          s = alert.riseDate().toString("yyyy-MM-dd hh:mm:ss,zzz");
          s.append(" ").append(alert.subscription().reminderMessage(alert))
              .append("\r\n");
          text.append(s);
          QString style =
              _config.params().paramUtf16(
                "mail.reminderstyle."+queue->_address,
                _config.params().paramUtf16(
                  "mail.reminderstyle", "background:#ffff80"));
          html.append("<li style=\"").append(style).append("\">")
              .append(s).append("</li>");
        }
      }
      // LATER clarify this message
      text.append(
            "\r\n"
            "Please note that there is a delay between alert rise and\r\n"
            "cancellation requests (timestamps above) and the actual time\r\n"
            "this mail is sent (send timestamp of the mail).\r\n");
      html.append(
            "</ul>\n"
            "<p>Please note that there is a delay between alert rise and "
            "cancellation requests (timestamps above) and the actual time this "
            "mail is sent (send timestamp of the mail).</p>\n");
      html.append("</body></html>\n");
      // mime handling
      QString body;
      if (_config.params().paramNumber<bool>("mail.enablehtml", true)) {
        headers.insert("Content-Type",
                       "multipart/alternative; boundary="+boundary);
        body = "--"+boundary
               +"\r\nContent-Type: text/plain;charset=utf-8\r\n\r\n"+text
               +"\r\n\r\n--"+boundary
               +"\r\nContent-Type: text/html;charset=utf-8\r\n\r\n"+html
               +"\r\n\r\n--"+boundary+"--\r\n";
      } else
        body = text;
      // queuing
      QString senderAddress =
          _config.params().paramUtf16(
            "mail.senderaddress."+queue->_address,
            _config.params().paramUtf16(
              "mail.senderaddress", "please-do-not-reply@localhost"));
      bool queued = _mailSender->send(senderAddress, recipients, body,
                                      QMultiHash<QString,QString>(headers),
                                      QList<QVariant>(), errorString);
      if (queued) {
        Log::info() << "successfuly sent an alert mail to " << addr
                    << " with " << queue->_alerts.size()
                    << " new alerts + " << queue->_cancellations.size()
                    << " cancellations + " << reminders.size()
                    << " reminders";      queue->_alerts.clear();
        queue->_cancellations.clear();
        queue->_lastMail = QDateTime::currentDateTime();
      } else {
        Log::warning() << "cannot send mail alert to " << addr
                       << " error in SMTP communication: " << errorString;
        queue->scheduleNext(this, RETRY_INTERVAL);
      }
    } else {
      Log::debug() << "MailAlertChannel::processQueue postponing send";
      queue->scheduleNext(this, std::max(minDelayBetweenSend-ms,1));
    }
  } else {
    //Log::debug() << "MailAlertChannel::processQueue called for nothing";
  }
}

void MailAlertChannel::asyncProcessing() {
  QDateTime now = QDateTime::currentDateTime();
  QDateTime tenSecondsAgo = now.addSecs(-10);
  QDateTime remindFrequencyAgo = now.addMSecs(-_config.remindPeriod());
  for (const MailAlertQueue *queue: _queues) {
    // check for queues with processing scheduled in the past
    // this should not happen but if time is not monotonic and some timers
    // cannot fire
    if (!queue->_nextProcessing.isNull()
        && queue->_nextProcessing < tenSecondsAgo)
      processQueue(queue->_address);
    // check for queues with pending reminders and no scheduled processing
    else if (!queue->_reminders.isEmpty()
             && queue->_lastMail < remindFrequencyAgo)
      processQueue(queue->_address);
  }
}
