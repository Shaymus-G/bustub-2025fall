//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include <memory>
#include <vector>
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new NestedLoopJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The nested loop join plan to be executed
 * @param left_executor The child executor that produces tuple for the left side of join
 * @param right_executor The child executor that produces tuple for the right side of join
 */
NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();

  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> left_tuples;
  std::vector<RID> left_rids;
  std::vector<Tuple> right_tuples;
  std::vector<RID> right_rids;

  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();

  while (left_executor_->Next(&left_tuples, &left_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &left_tuple : left_tuples) {
      bool matched = false;

      // 每处理一个左表 tuple，都重新初始化右表执行器，满足 NLJ 的执行方式检查。
      right_executor_->Init();

      while (right_executor_->Next(&right_tuples, &right_rids, BUSTUB_BATCH_SIZE)) {
        for (const auto &right_tuple : right_tuples) {
          bool should_join = true;

          if (plan_->Predicate() != nullptr) {
            auto pred = plan_->Predicate()->EvaluateJoin(&left_tuple, left_schema, &right_tuple, right_schema);
            should_join = !pred.IsNull() && pred.GetAs<bool>();
          }

          if (!should_join) {
            continue;
          }

          matched = true;

          std::vector<Value> values;
          values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());

          for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
            values.emplace_back(left_tuple.GetValue(&left_schema, i));
          }

          for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
            values.emplace_back(right_tuple.GetValue(&right_schema, i));
          }

          result_tuples_.emplace_back(values, &GetOutputSchema());
        }
      }

      if (!matched && plan_->GetJoinType() == JoinType::LEFT) {
        std::vector<Value> values;
        values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());

        for (uint32_t i = 0; i < left_schema.GetColumnCount(); i++) {
          values.emplace_back(left_tuple.GetValue(&left_schema, i));
        }

        for (uint32_t i = 0; i < right_schema.GetColumnCount(); i++) {
          values.emplace_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
        }

        result_tuples_.emplace_back(values, &GetOutputSchema());
      }
    }
  }
}

/**
 * Yield the next tuple batch from the join.
 * @param[out] tuple_batch The next tuple batch produced by the join
 * @param[out] rid_batch The next tuple RID batch produced by the join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto NestedLoopJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (cursor_ < result_tuples_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(result_tuples_[cursor_]);
    rid_batch->emplace_back();
    cursor_++;
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
