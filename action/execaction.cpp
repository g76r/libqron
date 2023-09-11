/* Copyright 2022-2023 Gregoire Barbier and others.
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
#include "execaction.h"
#include "action_p.h"
#include "sched/taskinstance.h"
#include <QProcess>
#include <QTimer>

class ExecActionData : public ActionData {
public:
  QString _command;
  ExecActionData(QString command = QString())
    : _command(command) { }
  Utf8String toString() const override {
    return "exec{ "+_command+" }";
  }
  Utf8String actionType() const override {
    return "exec"_u8;
  }
  void trigger(EventSubscription, ParamsProviderMerger *context,
               TaskInstance instance) const override {
    QString shell = "bash";
    QStringList cmdline;
    cmdline << shell << "-c"
            << PercentEvaluator::eval_utf16(_command, context);
    auto process = new QProcess;
    /*QObject::connect(process, &QProcess::errorOccurred,
                     [process,instance,command=_command](
                       QProcess::ProcessError error){
                       QString errorString =
                         "exec action '"+command+"' process failed: "
                         +QString::number(error)+" "+process->errorString();
                       if (instance.isNull())
                         Log::warning() << errorString;
                       else
                         Log::warning(instance.task().id(), instance.idAsLong())
                           << errorString;
                     });*/
    QObject::connect(process, &QProcess::finished,
                     [process,instance,command=_command](
                       int exitCode, QProcess::ExitStatus exitStatus) {
                       bool success = (exitStatus == QProcess::NormalExit
                                       && exitCode == 0);
                       process->deleteLater();
                       if (success)
                         return;
                       QString errorString =
                         "exec action '"+command+"' process failed: "
                         +QString::number(process->error())+" "
                         +process->errorString();
                       if (instance.isNull())
                         Log::warning() << errorString;
                       else
                         Log::warning(instance.task().id(), instance.idAsLong())
                           << errorString;
                     });
    //process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    QTimer::singleShot(10000, process, &QProcess::terminate);
    // LATER clear environment
    QString program = cmdline.takeFirst();
    process->start(program, cmdline);
  }
  PfNode toPfNode() const override {
    PfNode node(actionType(), _command);
    return node;
  }
};

ExecAction::ExecAction(Scheduler*, PfNode node)
  : Action(new ExecActionData(node.contentAsUtf16())) {
}

ExecAction::ExecAction(const ExecAction &rhs) : Action(rhs) {
}

ExecAction::~ExecAction() {
}
