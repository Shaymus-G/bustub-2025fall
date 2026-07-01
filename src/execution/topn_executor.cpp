//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// topn_executor.cpp
//
// Identification: src/execution/topn_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/topn_executor.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include "common/exception.h"

namespace bustub {

/**
 * Construct a new TopNExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The TopN plan to be executed
 */
TopNExecutor::TopNExecutor(ExecutorContext *exec_ctx, const TopNPlanNode *plan,
                           std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      cmp_(plan->GetOrderBy()),
      top_entries_(cmp_) {}

/** Initialize the TopN */
void TopNExecutor::Init() {
  child_executor_->Init();

  top_entries_ = std::priority_queue<SortEntry, std::vector<SortEntry>, TupleComparator>(cmp_);
  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : child_tuples) {
      SortEntry entry{GenerateSortKey(tuple, plan_->GetOrderBy(), child_executor_->GetOutputSchema()), tuple};

      if (top_entries_.size() < plan_->GetN()) {
        top_entries_.push(entry);
        continue;
      }

      if (plan_->GetN() == 0) {
        continue;
      }

      if (cmp_(entry, top_entries_.top())) {
        top_entries_.pop();
        top_entries_.push(entry);
      }
    }
  }

  std::vector<SortEntry> entries;
  entries.reserve(top_entries_.size());

  while (!top_entries_.empty()) {
    entries.push_back(top_entries_.top());
    top_entries_.pop();
  }

  std::sort(entries.begin(), entries.end(), cmp_);

  result_tuples_.reserve(entries.size());
  for (const auto &entry : entries) {
    result_tuples_.push_back(entry.second);
  }
}

/**
 * Yield the next tuple batch from the TopN.
 * @param[out] tuple_batch The next tuple batch produced by the TopN
 * @param[out] rid_batch The next tuple RID batch produced by the TopN
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto TopNExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
    -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (cursor_ < result_tuples_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(result_tuples_[cursor_]);
    rid_batch->emplace_back();
    cursor_++;
  }

  return !tuple_batch->empty();
}

auto TopNExecutor::GetNumInHeap() -> size_t { return top_entries_.size(); };

}  // namespace bustub
