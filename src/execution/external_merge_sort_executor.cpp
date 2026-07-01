//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include "common/macros.h"
#include "execution/plans/sort_plan.h"

namespace bustub {

namespace {
constexpr size_t SORT_TUPLES_PER_PAGE = 1024;

auto MakeSortEntry(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortEntry {
  return {GenerateSortKey(tuple, order_bys, schema), tuple};
}
}  // namespace

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      cmp_(plan->GetOrderBy()),
      child_executor_(std::move(child_executor)),
      bpm_(exec_ctx->GetBufferPoolManager()) {}

/** Initialize the external merge sort */
template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  child_executor_->Init();

  sorted_tuples_.clear();
  cursor_ = 0;

  std::vector<MergeSortRun> runs;
  std::vector<SortEntry> current_entries;
  current_entries.reserve(SORT_TUPLES_PER_PAGE);

  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    for (const auto &tuple : child_tuples) {
      current_entries.emplace_back(MakeSortEntry(tuple, plan_->GetOrderBy(), child_executor_->GetOutputSchema()));

      if (current_entries.size() == SORT_TUPLES_PER_PAGE) {
        std::stable_sort(current_entries.begin(), current_entries.end(), cmp_);

        std::vector<Tuple> run_tuples;
        run_tuples.reserve(current_entries.size());
        for (const auto &entry : current_entries) {
          run_tuples.push_back(entry.second);
        }

        runs.emplace_back(WriteTuplesToRun(run_tuples));
        current_entries.clear();
      }
    }
  }

  if (!current_entries.empty()) {
    std::stable_sort(current_entries.begin(), current_entries.end(), cmp_);

    std::vector<Tuple> run_tuples;
    run_tuples.reserve(current_entries.size());
    for (const auto &entry : current_entries) {
      run_tuples.push_back(entry.second);
    }

    runs.emplace_back(WriteTuplesToRun(run_tuples));
  }

  while (runs.size() > 1) {
    std::vector<MergeSortRun> next_runs;

    for (size_t i = 0; i < runs.size(); i += K) {
      if (i + 1 >= runs.size()) {
        next_runs.push_back(std::move(runs[i]));
        continue;
      }

      auto merged_run = MergeRuns(&runs[i], &runs[i + 1]);
      DeleteRunPages(runs[i]);
      DeleteRunPages(runs[i + 1]);
      next_runs.push_back(std::move(merged_run));
    }

    runs = std::move(next_runs);
  }

  if (runs.empty()) {
    return;
  }

  auto final_iter = runs[0].Begin();
  auto final_end = runs[0].End();

  while (final_iter != final_end) {
    sorted_tuples_.push_back(*final_iter);
    ++final_iter;
  }

  DeleteRunPages(runs[0]);
}

/**
 * Yield the next tuple batch from the external merge sort.
 * @param[out] tuple_batch The next tuple batch produced by the external merge sort.
 * @param[out] rid_batch The next tuple RID batch produced by the external merge sort.
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                        size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (cursor_ < sorted_tuples_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(sorted_tuples_[cursor_]);
    rid_batch->emplace_back();
    cursor_++;
  }

  return !tuple_batch->empty();
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::WriteTuplesToRun(const std::vector<Tuple> &tuples) -> MergeSortRun {
  std::vector<page_id_t> page_ids;

  if (tuples.empty()) {
    return {page_ids, bpm_};
  }

  page_id_t current_page_id = INVALID_PAGE_ID;
  WritePageGuard current_guard;
  IntermediateResultPage *current_page = nullptr;
  size_t current_tuple_count = 0;

  auto start_new_page = [&]() {
    current_page_id = bpm_->NewPage();
    BUSTUB_ENSURE(current_page_id != INVALID_PAGE_ID, "failed to allocate intermediate sort page");

    current_guard = bpm_->WritePage(current_page_id);
    current_page = current_guard.AsMut<IntermediateResultPage>();
    current_page->Init();
    current_tuple_count = 0;
  };

  auto finish_current_page = [&]() {
    if (current_page_id == INVALID_PAGE_ID) {
      return;
    }

    current_guard.Flush();
    current_guard.Drop();
    page_ids.push_back(current_page_id);
    current_page_id = INVALID_PAGE_ID;
    current_page = nullptr;
    current_tuple_count = 0;
  };

  start_new_page();

  for (const auto &tuple : tuples) {
    if (current_tuple_count >= SORT_TUPLES_PER_PAGE || !current_page->AppendTuple(tuple)) {
      finish_current_page();
      start_new_page();

      BUSTUB_ENSURE(current_page->AppendTuple(tuple), "tuple is too large for intermediate sort page");
      current_tuple_count = 1;
      continue;
    }

    current_tuple_count++;
  }

  finish_current_page();

  return {page_ids, bpm_};
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeRuns(MergeSortRun *left_run, MergeSortRun *right_run) -> MergeSortRun {
  std::vector<Tuple> output_tuples;
  output_tuples.reserve(SORT_TUPLES_PER_PAGE);

  std::vector<page_id_t> output_pages;

  auto flush_output = [&]() {
    if (output_tuples.empty()) {
      return;
    }

    auto run = WriteTuplesToRun(output_tuples);
    const auto &pages = run.GetPages();
    output_pages.insert(output_pages.end(), pages.begin(), pages.end());
    output_tuples.clear();
  };

  auto left_iter = left_run->Begin();
  auto left_end = left_run->End();
  auto right_iter = right_run->Begin();
  auto right_end = right_run->End();

  while (left_iter != left_end || right_iter != right_end) {
    Tuple next_tuple;

    if (left_iter == left_end) {
      next_tuple = *right_iter;
      ++right_iter;
    } else if (right_iter == right_end) {
      next_tuple = *left_iter;
      ++left_iter;
    } else {
      auto left_tuple = *left_iter;
      auto right_tuple = *right_iter;

      const auto left_entry = MakeSortEntry(left_tuple, plan_->GetOrderBy(), child_executor_->GetOutputSchema());
      const auto right_entry = MakeSortEntry(right_tuple, plan_->GetOrderBy(), child_executor_->GetOutputSchema());

      if (cmp_(right_entry, left_entry)) {
        next_tuple = right_tuple;
        ++right_iter;
      } else {
        next_tuple = left_tuple;
        ++left_iter;
      }
    }

    output_tuples.push_back(next_tuple);

    if (output_tuples.size() == SORT_TUPLES_PER_PAGE) {
      flush_output();
    }
  }

  flush_output();

  return {output_pages, bpm_};
}

template <size_t K>
void ExternalMergeSortExecutor<K>::DeleteRunPages(const MergeSortRun &run) {
  for (const auto page_id : run.GetPages()) {
    auto guard = bpm_->ReadPage(page_id);
    guard.Drop();
    BUSTUB_ENSURE(bpm_->DeletePage(page_id), "failed to delete intermediate sort page");
  }
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
