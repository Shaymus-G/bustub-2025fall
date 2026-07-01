//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/index_scan_executor.h"
#include <memory>
#include <vector>
#include "common/macros.h"
#include "storage/index/b_plus_tree_index.h"

namespace bustub {

/**
 * Creates a new index scan executor.
 * @param exec_ctx the executor context
 * @param plan the index scan plan to be executed
 */
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  BUSTUB_ASSERT(table_info_ != nullptr, "IndexScanExecutor: table not found");

  index_info_ = exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid());
  BUSTUB_ASSERT(index_info_ != nullptr, "IndexScanExecutor: index not found");

  result_rids_.clear();
  cursor_ = 0;

  auto *bpt_index = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  BUSTUB_ASSERT(bpt_index != nullptr, "IndexScanExecutor: expected BPlusTreeIndexForTwoIntegerColumn");

  if (!plan_->pred_keys_.empty()) {
    std::unordered_set<RID> seen;

    for (const auto &key_expr : plan_->pred_keys_) {
      auto key_value = key_expr->Evaluate(nullptr, GetOutputSchema());

      std::vector<Value> key_values{key_value};
      Tuple key_tuple{key_values, index_info_->index_->GetKeySchema()};

      std::vector<RID> lookup_result;
      bpt_index->ScanKey(key_tuple, &lookup_result, exec_ctx_->GetTransaction());

      for (const auto &rid : lookup_result) {
        if (seen.insert(rid).second) {
          result_rids_.push_back(rid);
        }
      }
    }
    return;
  }

  for (auto iter = bpt_index->GetBeginIterator(); !iter.IsEnd(); ++iter) {
    auto [key, rid] = *iter;
    result_rids_.push_back(rid);
  }
}

auto IndexScanExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                             size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (cursor_ < result_rids_.size() && tuple_batch->size() < batch_size) {
    const auto rid = result_rids_[cursor_++];

    auto [meta, tuple] = table_info_->table_->GetTuple(rid);
    if (meta.is_deleted_) {
      continue;
    }

    if (plan_->filter_predicate_ != nullptr) {
      auto pred = plan_->filter_predicate_->Evaluate(&tuple, GetOutputSchema());
      if (pred.IsNull() || !pred.GetAs<bool>()) {
        continue;
      }
    }

    tuple_batch->push_back(tuple);
    rid_batch->push_back(rid);
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
