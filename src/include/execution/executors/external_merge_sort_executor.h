//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "buffer/buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/page/intermediate_result_page.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  auto GetPages() const -> const std::vector<page_id_t> & { return pages_; }

  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;

    /**
     * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
     * next sort page.
     */
    auto operator++() -> Iterator & {
      if (run_ == nullptr || page_idx_ >= run_->pages_.size()) {
        return *this;
      }

      tuple_idx_++;

      while (page_idx_ < run_->pages_.size()) {
        auto guard = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
        const auto *page = guard.As<IntermediateResultPage>();

        if (tuple_idx_ < page->GetTupleCount()) {
          return *this;
        }

        page_idx_++;
        tuple_idx_ = 0;
      }

      return *this;
    }

    /**
     * Dereference the iterator to get the current tuple in the sorted run that the iterator is
     * pointing to.
     */
    auto operator*() -> Tuple {
      auto guard = run_->bpm_->ReadPage(run_->pages_[page_idx_]);
      const auto *page = guard.As<IntermediateResultPage>();
      return page->GetTuple(tuple_idx_);
    }

    /**
     * Checks whether two iterators are pointing to the same tuple in the same sorted run.
     */
    auto operator==(const Iterator &other) const -> bool {
      return run_ == other.run_ && page_idx_ == other.page_idx_ && tuple_idx_ == other.tuple_idx_;
    }

    /**
     * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
     * on different sorted runs.
     */
    auto operator!=(const Iterator &other) const -> bool { return !(*this == other); }

   private:
    Iterator(const MergeSortRun *run, size_t page_idx, uint32_t tuple_idx)
        : run_(run), page_idx_(page_idx), tuple_idx_(tuple_idx) {}

    explicit Iterator(const MergeSortRun *run) : run_(run) {}

    /** The sorted run that the iterator is iterating on. */
    /**
     * TODO(P3): Add your own private members here. You may want something to record your current
     * position in the sorted run. Also feel free to add additional constructors to initialize
     * your private members.
     */
    const MergeSortRun *run_{nullptr};

    /** 当前页在 run 中的下标。 */
    size_t page_idx_{0};

    /** 当前 tuple 在页内的下标。 */
    uint32_t tuple_idx_{0};
  };

  /**
   * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
   */
  auto Begin() -> Iterator {
    size_t page_idx = 0;

    while (page_idx < pages_.size()) {
      auto guard = bpm_->ReadPage(pages_[page_idx]);
      const auto *page = guard.As<IntermediateResultPage>();

      if (page->GetTupleCount() > 0) {
        return {this, page_idx, 0};
      }

      page_idx++;
    }

    return End();
  }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   */
  auto End() -> Iterator { return {this, pages_.size(), 0}; }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  BufferPoolManager *bpm_{nullptr};
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Spring 2025, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;

  /** TODO(P3): You will want to add your own private members here. */
  /** 子执行器。 */
  std::unique_ptr<AbstractExecutor> child_executor_;

  /** Buffer pool manager，用于读写外部排序中间页。 */
  BufferPoolManager *bpm_;

  /** 排序后的 tuple。 */
  std::vector<Tuple> sorted_tuples_;

  /** 当前输出到 sorted_tuples_ 的位置。 */
  size_t cursor_{0};

  /** 将一组有序 tuple 写成一个 run。 */
  auto WriteTuplesToRun(const std::vector<Tuple> &tuples) -> MergeSortRun;

  /** 合并两个有序 run。 */
  auto MergeRuns(MergeSortRun *left_run, MergeSortRun *right_run) -> MergeSortRun;

  /** 删除一个 run 占用的全部中间页。 */
  void DeleteRunPages(const MergeSortRun &run);
};

}  // namespace bustub
