//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"
#include <memory>
#include <vector>
#include "common/exception.h"
#include "common/macros.h"
#include "storage/index/b_plus_tree_index.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Creates a new nested index join executor.
 * @param exec_ctx the context that the nested index join should be performed in
 * @param plan the nested index join plan to be executed
 * @param child_executor the outer table
 */
NestedIndexJoinExecutor::NestedIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                                 std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  if (plan->GetJoinType() != JoinType::LEFT && plan->GetJoinType() != JoinType::INNER) {
    // Note for Spring 2025: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedIndexJoinExecutor::Init() {
  child_executor_->Init();

  inner_table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetInnerTableOid());
  BUSTUB_ASSERT(inner_table_info_ != nullptr, "NestedIndexJoinExecutor: inner table not found");

  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
  BUSTUB_ASSERT(index_info_ != nullptr, "NestedIndexJoinExecutor: index not found");

  auto *bpt_index = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  BUSTUB_ASSERT(bpt_index != nullptr, "NestedIndexJoinExecutor: expected BPlusTreeIndexForTwoIntegerColumn");

  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> outer_tuples;
  std::vector<RID> outer_rids;

  const auto &outer_schema = child_executor_->GetOutputSchema();
  const auto &inner_schema = plan_->InnerTableSchema();

  while (child_executor_->Next(&outer_tuples, &outer_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &outer_tuple : outer_tuples) {
      auto key_value = plan_->KeyPredicate()->Evaluate(&outer_tuple, outer_schema);

      std::vector<Value> key_values{key_value};
      Tuple key_tuple{key_values, index_info_->index_->GetKeySchema()};

      std::vector<RID> lookup_result;
      bpt_index->ScanKey(key_tuple, &lookup_result, exec_ctx_->GetTransaction());

      bool matched = false;

      for (const auto &inner_rid : lookup_result) {
        auto [meta, inner_tuple] = inner_table_info_->table_->GetTuple(inner_rid);
        if (meta.is_deleted_) {
          continue;
        }

        matched = true;

        std::vector<Value> values;
        values.reserve(outer_schema.GetColumnCount() + inner_schema.GetColumnCount());

        for (uint32_t i = 0; i < outer_schema.GetColumnCount(); i++) {
          values.emplace_back(outer_tuple.GetValue(&outer_schema, i));
        }

        for (uint32_t i = 0; i < inner_schema.GetColumnCount(); i++) {
          values.emplace_back(inner_tuple.GetValue(&inner_schema, i));
        }

        result_tuples_.emplace_back(values, &GetOutputSchema());
      }

      if (!matched && plan_->GetJoinType() == JoinType::LEFT) {
        std::vector<Value> values;
        values.reserve(outer_schema.GetColumnCount() + inner_schema.GetColumnCount());

        for (uint32_t i = 0; i < outer_schema.GetColumnCount(); i++) {
          values.emplace_back(outer_tuple.GetValue(&outer_schema, i));
        }

        for (uint32_t i = 0; i < inner_schema.GetColumnCount(); i++) {
          values.emplace_back(ValueFactory::GetNullValueByType(inner_schema.GetColumn(i).GetType()));
        }

        result_tuples_.emplace_back(values, &GetOutputSchema());
      }
    }
  }
}

auto NestedIndexJoinExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
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
