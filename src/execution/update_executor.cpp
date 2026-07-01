//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/update_executor.h"
#include <memory>
#include <vector>
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new UpdateExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The update plan to be executed
 * @param child_executor The child executor that feeds the update
 */
UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the update */
void UpdateExecutor::Init() {
  child_executor_->Init();

  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid());
  BUSTUB_ASSERT(table_info_ != nullptr, "UpdateExecutor: table not found");

  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  emitted_ = false;
}

/**
 * Yield the number of rows updated in the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows updated in the table
 * @param[out] rid_batch The next tuple RID batch produced by the update (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: UpdateExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: UpdateExecutor::Next() returns true with the number of updated rows produced only once.
 */
auto UpdateExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (emitted_) {
    return false;
  }
  emitted_ = true;

  int32_t update_count = 0;

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, batch_size)) {
    BUSTUB_ASSERT(child_tuples.size() == child_rids.size(), "UpdateExecutor: tuple/rid batch size mismatch");

    for (size_t i = 0; i < child_tuples.size(); i++) {
      const auto &old_tuple = child_tuples[i];
      const auto old_rid = child_rids[i];

      auto old_meta = table_info_->table_->GetTupleMeta(old_rid);
      if (old_meta.is_deleted_) {
        continue;
      }

      std::vector<Value> new_values;
      new_values.reserve(plan_->target_expressions_.size());

      for (const auto &expr : plan_->target_expressions_) {
        new_values.push_back(expr->Evaluate(&old_tuple, child_executor_->GetOutputSchema()));
      }

      Tuple new_tuple{new_values, &table_info_->schema_};

      old_meta.is_deleted_ = true;
      table_info_->table_->UpdateTupleMeta(old_meta, old_rid);

      for (const auto &index_info : indexes_) {
        auto old_key = old_tuple.KeyFromTuple(table_info_->schema_, *index_info->index_->GetKeySchema(),
                                              index_info->index_->GetKeyAttrs());
        index_info->index_->DeleteEntry(old_key, old_rid, exec_ctx_->GetTransaction());
      }

      auto new_rid_opt = table_info_->table_->InsertTuple(TupleMeta{0, false}, new_tuple, exec_ctx_->GetLockManager(),
                                                          exec_ctx_->GetTransaction(), table_info_->oid_);
      if (!new_rid_opt.has_value()) {
        continue;
      }

      const auto new_rid = new_rid_opt.value();

      for (const auto &index_info : indexes_) {
        auto new_key = new_tuple.KeyFromTuple(table_info_->schema_, *index_info->index_->GetKeySchema(),
                                              index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(new_key, new_rid, exec_ctx_->GetTransaction());
      }

      update_count++;
    }
  }

  tuple_batch->emplace_back(std::vector<Value>{ValueFactory::GetIntegerValue(update_count)}, &GetOutputSchema());
  return true;
}

}  // namespace bustub
