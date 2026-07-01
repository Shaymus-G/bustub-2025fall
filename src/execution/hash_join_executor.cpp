//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"
#include <memory>
#include <vector>
#include "common/exception.h"
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new HashJoinExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The HashJoin join plan to be executed
 * @param left_child The child executor that produces tuples for the left side of join
 * @param right_child The child executor that produces tuples for the right side of join
 */
HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

/** Initialize the join */
void HashJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();

  hash_table_.clear();
  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> right_tuples;
  std::vector<RID> right_rids;

  while (right_child_->Next(&right_tuples, &right_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &right_tuple : right_tuples) {
      auto key = MakeRightJoinKey(&right_tuple);

      if (HasNullKey(key)) {
        continue;
      }

      hash_table_[key].push_back(right_tuple);
    }
  }

  std::vector<Tuple> left_tuples;
  std::vector<RID> left_rids;

  const auto &left_schema = left_child_->GetOutputSchema();
  const auto &right_schema = right_child_->GetOutputSchema();

  while (left_child_->Next(&left_tuples, &left_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &left_tuple : left_tuples) {
      auto key = MakeLeftJoinKey(&left_tuple);
      bool matched = false;

      if (!HasNullKey(key)) {
        auto iter = hash_table_.find(key);
        if (iter != hash_table_.end()) {
          for (const auto &right_tuple : iter->second) {
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
 * Yield the next tuple batch from the hash join.
 * @param[out] tuple_batch The next tuple batch produced by the hash join
 * @param[out] rid_batch The next tuple RID batch produced by the hash join
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto HashJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
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
