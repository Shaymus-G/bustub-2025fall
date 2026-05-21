//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.h
//
// Identification: src/include/storage/index/index_iterator.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <memory>
#include <utility>
#include "buffer/traced_buffer_pool_manager.h"
#include "common/config.h"
#include "common/macros.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator, NumTombs>
#define SHORT_INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class IndexIterator {
 public:
  // you may define your own constructor based on your member variables
  IndexIterator();

  // 用 leaf 的读 guard 构造 iterator，使 iterator 解引用时 page 数据仍被保护。
  IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, ReadPageGuard leaf_guard, int index);

  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    // 两个 end iterator 视为相等
    if (is_end_ && itr.is_end_) {
      return true;
    }
    // 一个 end、一个非 end，则不相等
    if (is_end_ != itr.is_end_) {
      return false;
    }
    // 非 end iterator 用 leaf page_id 和 leaf 内 index 判断是否相等
    return leaf_guard_.GetPageId() == itr.leaf_guard_.GetPageId() && index_ == itr.index_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool {
    // != 直接复用 ==，避免比较逻辑不一致
    return !(*this == itr);
  }

 private:
  // add your own private member variables here

  // 跳过 tombstone entry；如果当前 leaf 到尾，则沿 next_page_id_ 继续扫描
  void SkipTombstones();

  // 跨 leaf 前进时需要通过 bpm_ 读取 next leaf
  std::shared_ptr<TracedBufferPoolManager> bpm_{nullptr};

  // 当前 leaf 的读 guard，保证当前 page 在 iterator 生命周期内有效
  ReadPageGuard leaf_guard_{};

  // 当前 leaf 内的数组下标
  int index_{0};

  // 是否已经到达 end
  bool is_end_{true};
};

}  // namespace bustub
