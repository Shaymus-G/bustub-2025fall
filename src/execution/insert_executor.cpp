//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/insert_executor.h"
#include <memory>
#include <vector>
#include "common/macros.h"
#include "type/value_factory.h"

namespace bustub {

/**
 * Construct a new InsertExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The insert plan to be executed
 * @param child_executor The child executor from which inserted tuples are pulled
 */
InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the insert */
void InsertExecutor::Init() {
  child_executor_->Init();

  table_info_ = exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid());
  BUSTUB_ASSERT(table_info_ != nullptr, "InsertExecutor: table not found");

  indexes_ = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);
  emitted_ = false;
}

/**
 * Yield the number of rows inserted into the table.
 * @param[out] tuple_batch The tuple batch with one integer indicating the number of rows inserted into the table
 * @param[out] rid_batch The next tuple RID batch produced by the insert (ignore, not used)
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 *
 * NOTE: InsertExecutor::Next() does not use the `rid_batch` out-parameter.
 * NOTE: InsertExecutor::Next() returns true with the number of inserted rows produced only once.
 */
auto InsertExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                          size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (emitted_) {
    return false;
  }
  emitted_ = true;

  int32_t insert_count = 0;

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, batch_size)) {
    for (const auto &tuple : child_tuples) {
      auto rid_opt = table_info_->table_->InsertTuple(TupleMeta{0, false}, tuple, exec_ctx_->GetLockManager(),
                                                      exec_ctx_->GetTransaction(), table_info_->oid_);
      if (!rid_opt.has_value()) {
        continue;
      }

      const auto rid = rid_opt.value();

      for (const auto &index_info : indexes_) {
        auto key = tuple.KeyFromTuple(table_info_->schema_, *index_info->index_->GetKeySchema(),
                                      index_info->index_->GetKeyAttrs());
        index_info->index_->InsertEntry(key, rid, exec_ctx_->GetTransaction());
      }

      insert_count++;
    }
  }

  tuple_batch->emplace_back(std::vector<Value>{ValueFactory::GetIntegerValue(insert_count)}, &GetOutputSchema());
  return true;
}

}  // namespace bustub
