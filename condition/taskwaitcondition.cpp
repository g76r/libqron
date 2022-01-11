/* Copyright 2022 Gregoire Barbier and others.
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
#include "taskwaitcondition.h"
#include "condition_p.h"
#include "util/containerutils.h"

static QHash<TaskWaitOperator,QString> _operatorsAsString {
  { AllFinished, "allfinished" },
  { AnyFinished, "anyfinished" },
  { AllSuccess, "allsuccess" },
  { AllFailure, "allfailure" },
  { AllCanceled, "allcanceled" },
  { NoSuccess, "nosuccess" },
  { NoFailure, "nofailure" },
  { NoCanceled, "nocanceled" },
  { AnySuccess, "anysuccess" },
  { AnyFailure, "anyfailure" },
  { AnyCanceled, "anycanceled" },
  { AnyNonSuccess, "anynonsuccess" },
  { AnyNonFailure, "anynonfailure" },
  { AnyNonCanceled, "anynoncanceled" },
  { AllFinishedAnySuccess, "allfinishedanysuccess" },
  { AllFinishedAnyFailure, "allfinishedanyfailure" },
  { AllFinishedAnyCanceled, "allfinishedanycanceled" },
  { IsEmpty, "isempty" },
  { IsNotEmpty, "isnotempty" },
  { True, "true" },
  { False, "false" },
};

static QHash<TaskWaitOperator,TaskWaitOperator>
_cancelOperatorFromQueueOperator {
  { AllFinished, False },
  { AnyFinished, IsEmpty },
  { AllSuccess, AnyNonSuccess },
  { AllFailure, AnyNonFailure },
  { AllCanceled, AnyNonCanceled },
  { NoSuccess, AnySuccess },
  { NoFailure, AnyFailure },
  { NoCanceled, AnyCanceled },
  { AnySuccess, NoSuccess },
  { AnyFailure, NoFailure },
  { AnyCanceled, NoCanceled },
  { AnyNonSuccess, AllSuccess },
  { AnyNonFailure, AllFailure },
  { AnyNonCanceled, AllCanceled },
  { AllFinishedAnySuccess, NoSuccess },
  { AllFinishedAnyFailure, NoFailure },
  { AllFinishedAnyCanceled, NoCanceled },
  { IsEmpty, IsNotEmpty },
  { IsNotEmpty, IsEmpty },
  { True, False },
  { False, True },
};

static QHash<QString,TaskWaitOperator> _operatorsFromString {
  ContainerUtils::reversed(_operatorsAsString)
};

TaskWaitOperator TaskWaitCondition::operatorFromString(QString op) {
  return _operatorsFromString.value(op, UnknownOperator);
}

QString TaskWaitCondition::operatorAsString(TaskWaitOperator op) {
  return _operatorsAsString.value(op, QString());
}

TaskWaitOperator TaskWaitCondition::cancelOperatorFromQueueOperator(
    TaskWaitOperator op) {
  return _cancelOperatorFromQueueOperator.value(op, UnknownOperator);
}

namespace {

class Counters {
  quint32 unfinished = 0;
  quint32 canceled = 0;
  quint32 failure = 0;
  quint32 success = 0;

public:
  Counters(TaskInstanceList tasks) {
    for (auto task: tasks) {
      switch(task.status()) {
      case TaskInstance::Canceled:
        ++canceled;
        break;
      case TaskInstance::Failure:
        ++failure;
        break;
      case TaskInstance::Success:
        ++success;
        break;
      case TaskInstance::Queued:
      case TaskInstance::Running:
      case TaskInstance::Waiting:
        ++unfinished;
      }
    }
  }

  bool evaluate(TaskWaitOperator op) {
    switch(op) {
    case AllFinished:
      return unfinished == 0;
    case AnyFinished:
      return canceled + failure + success;
    case AllSuccess:
      return unfinished + canceled + failure == 0;
    case AllFailure:
      return unfinished + canceled + success == 0;
    case AllCanceled:
      return unfinished + failure + success == 0;
    case NoSuccess:
      return unfinished + success == 0;
    case NoFailure:
      return unfinished + failure == 0;
    case NoCanceled:
      return unfinished + canceled == 0;
    case AnySuccess:
      return success;
    case AnyFailure:
      return failure;
    case AnyCanceled:
      return canceled;
    case AnyNonSuccess:
      return canceled + failure;
    case AnyNonFailure:
      return canceled + success;
    case AnyNonCanceled:
      return failure + success;
    case AllFinishedAnySuccess:
      return unfinished == 0 && success;
    case AllFinishedAnyFailure:
      return unfinished == 0 && failure;
    case AllFinishedAnyCanceled:
      return unfinished == 0 && canceled;
    case IsEmpty:
      return unfinished + canceled + failure + success == 0;
    case IsNotEmpty:
      return unfinished + canceled + failure + success;
    case True:
      return true;
    case False:
    case UnknownOperator:
      ;
    }
    return false;
  }
};

} // unnamed namespace

class TaskWaitConditionData : public ConditionData {
public:
  TaskWaitOperator _op;
  QList<quint64> _ids;

  TaskWaitConditionData(TaskWaitOperator op, QList<quint64> ids)
    : _op(op), _ids(ids) { }
  TaskWaitConditionData() : _op(UnknownOperator) { }
  QString toString() const override {
    QString s = conditionType();
    for (auto id: _ids)
      s += ' ' + QString::number(id);
    return s;
  }
  QString conditionType() const override {
    return TaskWaitCondition::operatorAsString(_op);
  }
  bool evaluate(ParamSet eventContext, TaskInstance instance) const override {
    Q_UNUSED(eventContext)
    return Counters(instance.herder().herdedTasks()).evaluate(_op);
  }
  PfNode toPfNode() const override {
    PfNode node(conditionType());
    QString s;
    for (auto id: _ids)
      s += QString::number(id) + ' ';
    s.chop(1);
    node.setContent(s);
    return node;
  }
};

TaskWaitCondition::TaskWaitCondition(PfNode node) {
  TaskWaitOperator op = operatorFromString(node.name());
  if (op == UnknownOperator)
    return;
  auto list = node.contentAsStringList();
  QList<quint64> ids;
  for (auto item: list) {
    bool ok;
    quint64 id = item.toULongLong(&ok);
    if (!ok)
      return;
    ids << id;
  }
  d = new TaskWaitConditionData(op, ids);
}

TaskWaitCondition::TaskWaitCondition(const TaskWaitCondition &other)
  : Condition(other) {
}

TaskWaitCondition::~TaskWaitCondition() {
}
