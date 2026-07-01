//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/delete_executor.h"
#include <memory>
#include <vector>
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new DeleteExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The delete plan to be executed
 * @param child_executor The child executor that feeds the delete
 */
DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the delete */
void DeleteExecutor::Init() {
  child_executor_->Init();

  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid());
  BUSTUB_ASSERT(table_info_ != nullptr, "DeleteExecutor: table not found");

  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  emitted_ = false;
}

/**
 * Yield the number of rows deleted from the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows deleted from the table
 * @param[out] rid_batch The next tuple RID batch produced by the delete (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: DeleteExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: DeleteExecutor::Next() returns true with the number of deleted rows produced only once.
 */
auto DeleteExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (emitted_) {
    return false;
  }
  emitted_ = true;

  int32_t delete_count = 0;

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, batch_size)) {
    BUSTUB_ASSERT(child_tuples.size() == child_rids.size(), "DeleteExecutor: tuple/rid batch size mismatch");

    for (size_t i = 0; i < child_tuples.size(); i++) {
      const auto &tuple = child_tuples[i];
      const auto rid = child_rids[i];

      auto meta = table_info_->table_->GetTupleMeta(rid);
      if (meta.is_deleted_) {
        continue;
      }

      meta.is_deleted_ = true;
      table_info_->table_->UpdateTupleMeta(meta, rid);

      for (const auto &index_info : indexes_) {
        auto key = tuple.KeyFromTuple(table_info_->schema_, *index_info->index_->GetKeySchema(),
                                      index_info->index_->GetKeyAttrs());
        index_info->index_->DeleteEntry(key, rid, exec_ctx_->GetTransaction());
      }

      delete_count++;
    }
  }

  tuple_batch->emplace_back(std::vector<Value>{ValueFactory::GetIntegerValue(delete_count)}, &GetOutputSchema());
  return true;
}

}  // namespace bustub
