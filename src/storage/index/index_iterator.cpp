//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_iterator.cpp
//
// Identification: src/storage/index/index_iterator.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/**
 * @note you can change the destructor/constructor method here
 * set your own input parameters
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator() = default;

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(std::shared_ptr<TracedBufferPoolManager> bpm, ReadPageGuard leaf_guard, int index)
    : bpm_(std::move(bpm)), leaf_guard_(std::move(leaf_guard)), index_(index), is_end_(false) {
  // 构造后立即跳过 tombstone，保证 iterator 指向的是对外可见的 entry
  SkipTombstones();
}

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(const IndexIterator &other)
    : bpm_(other.bpm_), index_(other.index_), is_end_(other.is_end_) {
  // end iterator 不持有 leaf guard；非 end iterator 复制时重新读当前 leaf
  if (!is_end_ && bpm_ != nullptr) {
    leaf_guard_ = bpm_->ReadPage(other.leaf_guard_.GetPageId());
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator=(const IndexIterator &other) -> IndexIterator & {
  // 复制赋值时先释放当前 guard，再重新读取 other 当前所在 leaf
  if (this == &other) {
    return *this;
  }
  leaf_guard_.Drop();
  bpm_ = other.bpm_;
  index_ = other.index_;
  is_end_ = other.is_end_;
  if (!is_end_ && bpm_ != nullptr) {
    leaf_guard_ = bpm_->ReadPage(other.leaf_guard_.GetPageId());
  }
  return *this;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool {
  // end iterator 不持有有效 leaf 位置
  return is_end_;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  // 解引用只能发生在非 end iterator 上
  assert(!is_end_);
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;
  auto leaf_page = leaf_guard_.template As<LeafPage>();
  assert(index_ >= 0);
  assert(index_ < leaf_page->GetSize());
  assert(!leaf_page->IsTombstoned(index_));
  return std::pair<const KeyType &, const ValueType &>(leaf_page->KeyAtRef(index_), leaf_page->ValueAtRef(index_));
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  // end iterator 自增仍保持 end
  if (is_end_) {
    return *this;
  }
  index_++;
  SkipTombstones();
  return *this;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void INDEXITERATOR_TYPE::SkipTombstones() {
  // 跳过 tombstone；如果当前 leaf 到尾，则沿 next_page_id_ 读取下一个 leaf
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;
  while (!is_end_) {
    auto leaf_page = leaf_guard_.template As<LeafPage>();
    while (index_ < leaf_page->GetSize() && leaf_page->IsTombstoned(index_)) {
      index_++;
    }
    if (index_ < leaf_page->GetSize()) {
      return;
    }
    page_id_t next_page_id = leaf_page->GetNextPageId();
    if (next_page_id == INVALID_PAGE_ID) {
      leaf_guard_.Drop();
      is_end_ = true;
      index_ = 0;
      return;
    }
    leaf_guard_ = bpm_->ReadPage(next_page_id);
    index_ = 0;
  }
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
