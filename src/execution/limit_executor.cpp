//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// limit_executor.cpp
//
// Identification: src/execution/limit_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/limit_executor.h"
#include <algorithm>
#include <memory>
#include <vector>
#include "common/macros.h"

namespace bustub {

/**
 * Construct a new LimitExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The limit plan to be executed
 * @param child_executor The child executor from which limited tuples are pulled
 */
LimitExecutor::LimitExecutor(ExecutorContext *exec_ctx, const LimitPlanNode *plan,
                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the limit */
void LimitExecutor::Init() {
  child_executor_->Init();
  emitted_count_ = 0;
}

/**
 * Yield the next tuple batch from the limit.
 * @param[out] tuple_batch The next tuple batch produced by the limit
 * @param[out] rid_batch The next tuple RID batch produced by the limit
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto LimitExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                         size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  if (emitted_count_ >= plan_->GetLimit()) {
    return false;
  }

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (tuple_batch->size() < batch_size && emitted_count_ < plan_->GetLimit()) {
    const auto remaining_limit = plan_->GetLimit() - emitted_count_;
    const auto request_size = std::min(batch_size - tuple_batch->size(), remaining_limit);

    if (!child_executor_->Next(&child_tuples, &child_rids, request_size)) {
      break;
    }

    for (size_t i = 0;
         i < child_tuples.size() && tuple_batch->size() < batch_size && emitted_count_ < plan_->GetLimit(); i++) {
      tuple_batch->push_back(child_tuples[i]);
      rid_batch->push_back(child_rids[i]);
      emitted_count_++;
    }
  }

  return !tuple_batch->empty();
}

}  // namespace bustub
