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
  Counters(QList<quint64> ids, TaskInstanceList tasks) {
    for (auto task: tasks) {
      quint64 id = task.idAsLong();
      if (!ids.contains(id))
        continue;
      //ids.removeAll(id);
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
      case TaskInstance::Planned:
      case TaskInstance::Queued:
      case TaskInstance::Running:
      case TaskInstance::Waiting:
        ++unfinished;
      }
    }
    //unfinished += ids.size();
    //qDebug() << "Counters" << ids << tasks << unfinished << canceled
    //          << failure << success;
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
  QString _expr;

  TaskWaitConditionData(TaskWaitOperator op, QString expr)
    : _op(op), _expr(expr) { }
  TaskWaitConditionData() : _op(UnknownOperator) { }
  QString toString() const override {
    return TaskWaitCondition::operatorAsString(_op)+' '+_expr;
  }
  QString conditionType() const override {
    return "taskwait";
  }
  bool evaluate(TaskInstance instance, ParamSet) const override {
    return Counters(evaluateIds(instance.herder()),
                    instance.herder().herdedTasks()).evaluate(_op);
  }
  PfNode toPfNode() const override {
    return PfNode(TaskWaitCondition::operatorAsString(_op), _expr);
  }
  QList<quint64> evaluateIds(TaskInstance herder) const;
};

TaskWaitCondition::TaskWaitCondition(PfNode node) {
  TaskWaitOperator op = operatorFromString(node.name());
  if (op == UnknownOperator)
    return;
  d = new TaskWaitConditionData(op, node.contentAsString());
}

TaskWaitCondition::TaskWaitCondition(TaskWaitOperator op, QString expr)
    : Condition(new TaskWaitConditionData(op, expr)) {
}

TaskWaitCondition::TaskWaitCondition(const TaskWaitCondition &other)
  : Condition(other) {
}

TaskWaitCondition::~TaskWaitCondition() {
}

TaskWaitOperator TaskWaitCondition::op() const {
  auto data = static_cast<const TaskWaitConditionData*>(d.data());
  return data ? data->_op : UnknownOperator;
}

QString TaskWaitCondition::expr() const {
  auto data = static_cast<const TaskWaitConditionData*>(d.data());
  return data ? data->_expr : QString();
}

QList<quint64> TaskWaitConditionData::evaluateIds(TaskInstance herder) const {
  QList<quint64> ids;
  auto ppp = herder.pseudoParams();
  auto value = herder.params().evaluate(_expr, &ppp);
  auto list = value.split(' ', Qt::SkipEmptyParts);
  for (auto item: list) {
    bool ok;
    quint64 id = item.toULongLong(&ok);
    if (ok)
      ids << id;
  }
  //qDebug() << "evaluateIds" << herder.idAsLong()
  //         << TaskWaitCondition::operatorAsString(_op) << _expr
  //         << herder.params().evaluate(_expr, &ppp)
  //         << ids;
  return ids;
}