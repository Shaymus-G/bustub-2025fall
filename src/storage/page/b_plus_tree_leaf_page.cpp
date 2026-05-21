//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>
#include <cassert>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new leaf page
 *
 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
 * including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size.
 *
 * @param max_size Max size of the leaf node
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  // 新建 leaf page，初始化公共 header，并将叶子链表指针置为空
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  SetNextPageId(INVALID_PAGE_ID);
  // tombstone buffer 初始为空，表示还没有任何 pending delete
  num_tombstones_ = 0;
}

/**
 * @brief Helper function for fetching tombstones of a page.
 * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstones() const -> std::vector<KeyType> {
  // tombstones_ 返回时需要转换为对应的 key，并保持 oldest -> newest 的顺序
  std::vector<KeyType> result;
  result.reserve(num_tombstones_);
  for (size_t i = 0; i < num_tombstones_; i++) {
    auto index = static_cast<int>(tombstones_[i]);
    assert(index >= 0);
    assert(index < GetSize());
    result.push_back(key_array_[index]);
  }
  return result;
}

/**
 * Helper methods to set/get next page id
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t {
  return next_page_id_;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) {
  next_page_id_ = next_page_id;
}

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // Leaf page 的 key 从 index == 0 开始全部有效
  assert(index >= 0);
  assert(index < GetSize());
  return key_array_[index];
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // Leaf page 的 value 是 RID，与 KeyAt(index) 一一对应
  assert(index >= 0);
  assert(index < GetSize());
  return rid_array_[index];
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAtRef(int index) const -> const KeyType & {
  // iterator 解引用时需要返回 key 引用，因此这里直接返回 key_array_ 中的元素
  assert(index >= 0);
  assert(index < GetSize());
  return key_array_[index];
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAtRef(int index) const -> const ValueType & {
  // iterator 解引用时需要返回 value 引用，因此这里直接返回 rid_array_ 中的元素
  assert(index >= 0);
  assert(index < GetSize());
  return rid_array_[index];
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  // 设置 key 时允许 index 指向当前 size 以内或即将插入的位置
  assert(index >= 0);
  assert(index < GetMaxSize());
  key_array_[index] = key;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  // 设置 RID 时允许 index 指向当前 size 以内或即将插入的位置
  assert(index >= 0);
  assert(index < GetMaxSize());
  rid_array_[index] = value;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNumTombstones() const -> size_t {
  // 返回 tombstone buffer 当前使用量，便于 Remove 判断是否需要触发物理删除
  return num_tombstones_;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetMaxTombstones() const -> size_t {
  // LEAF_PAGE_TOMB_CNT 是由模板参数 NumTombs 决定的编译期容量
  // B+Tree 删除优化路径用它判断下一次 AddTombstone 是否会触发物理删除
  return LEAF_PAGE_TOMB_CNT;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::IsTombstoned(int index) const -> bool {
  // tombstone buffer 中保存的是被逻辑删除的物理下标
  // 只要某个 index 出现在 tombstones_ 中，就说明该 entry 对外不可见
  assert(index >= 0);
  assert(index < GetSize());
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (static_cast<int>(tombstones_[i]) == index) {
      return true;
    }
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveTombstoneAt(size_t tombstone_pos) {
  // 删除 tombstone buffer 中的一个记录时，只压缩 tombstones_ 数组
  assert(tombstone_pos < num_tombstones_);
  for (size_t i = tombstone_pos + 1; i < num_tombstones_; i++) {
    tombstones_[i - 1] = tombstones_[i];
  }
  num_tombstones_--;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::AddTombstone(int index) {
  assert(index >= 0);
  assert(index < GetSize());
  // 如果 tombstone 功能关闭，则删除操作直接转化为物理删除
  if constexpr (LEAF_PAGE_TOMB_CNT == 0) {
    DeleteAt(index);
    return;
  }
  // 如果该位置已经被 tombstone 标记，则不重复加入
  if (IsTombstoned(index)) {
    return;
  }
  // tombstone buffer 满时，先删除最旧的 tombstone，删除会导致后续下标左移，因此新删除项的 index 也需要同步修正
  if (num_tombstones_ == LEAF_PAGE_TOMB_CNT) {
    auto old_index = static_cast<int>(tombstones_[0]);
    RemoveTombstoneAt(0);
    DeleteAt(old_index);
    if (index > old_index) {
      index--;
    }
  }
  // 将本次删除记录追加到 buffer 末尾，保持 oldest -> newest 的顺序
  tombstones_[num_tombstones_] = static_cast<size_t>(index);
  num_tombstones_++;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::InsertAt(int index, const KeyType &key, const ValueType &value) {
  // 在物理数组中插入新 entry 时，插入位置及其右侧元素会整体右移，
  // 因此所有 tombstone 下标如果大于等于插入位置，也必须同步加一
  assert(index >= 0);
  assert(index <= GetSize());
  assert(GetSize() < GetMaxSize());
  for (int i = GetSize(); i > index; i--) {
    key_array_[i] = key_array_[i - 1];
    rid_array_[i] = rid_array_[i - 1];
  }
  key_array_[index] = key;
  rid_array_[index] = value;
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (static_cast<int>(tombstones_[i]) >= index) {
      tombstones_[i]++;
    }
  }
  ChangeSizeBy(1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::DeleteAt(int index) {
  // 物理删除 entry 时，需要同时维护 tombstone buffer
  // 等于 index 的 tombstone 记录应被移除，大于 index 的 tombstone 下标需要左移
  assert(index >= 0);
  assert(index < GetSize());
  size_t i = 0;
  while (i < num_tombstones_) {
    if (static_cast<int>(tombstones_[i]) == index) {
      RemoveTombstoneAt(i);
      continue;
    }
    if (static_cast<int>(tombstones_[i]) > index) {
      tombstones_[i]--;
    }
    i++;
  }
  for (int j = index + 1; j < GetSize(); j++) {
    key_array_[j - 1] = key_array_[j];
    rid_array_[j - 1] = rid_array_[j];
  }
  ChangeSizeBy(-1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveTombstoneForIndex(int index) -> bool {
  // 重新插入已经 tombstone 的 key 时，只需要取消 tombstone 标记并更新 RID
  assert(index >= 0);
  assert(index < GetSize());
  for (size_t i = 0; i < num_tombstones_; i++) {
    if (static_cast<int>(tombstones_[i]) == index) {
      RemoveTombstoneAt(i);
      return true;
    }
  }
  return false;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ApplyAllTombstones() {
  // split / merge 前把 tombstone 全部物理删除
  while (num_tombstones_ > 0) {
    auto index = static_cast<int>(tombstones_[0]);
    DeleteAt(index);
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::TombstoneAt(size_t pos) const -> size_t {
  // tombstone buffer 按 oldest -> newest 顺序保存物理下标
  assert(pos < num_tombstones_);
  return tombstones_[pos];
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::AppendTombstone(size_t index) {
  // 搬移 tombstone 时追加到 buffer 尾部，表示它比当前已有 tombstone 更新
  assert(static_cast<int>(index) >= 0);
  assert(static_cast<int>(index) < GetSize());
  if constexpr (LEAF_PAGE_TOMB_CNT == 0) {
    DeleteAt(static_cast<int>(index));
    return;
  }
  AddTombstone(static_cast<int>(index));
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::ClearTombstones() {
  // source leaf 被合并走后，不再需要保留 tombstone 记录
  num_tombstones_ = 0;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::AddTombstoneByKey(const KeyType &key, const KeyComparator &comparator) {
  // tombstone 迁移时不要复用旧下标，因为 AddTombstone 可能触发物理删除并改变数组位置
  // 每次按 key 重新定位，可以保证 tombstone 最终指向正确 entry
  for (int i = 0; i < GetSize(); i++) {
    if (comparator(KeyAt(i), key) == 0) {
      AddTombstone(i);
      return;
    }
  }
  BUSTUB_ENSURE(false, "tombstone key not found in leaf page");
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNumVisibleEntries() const -> int {
  // 统计未被 tombstone 标记的 entry，用于判断逻辑上是否为空
  int visible = 0;
  for (int i = 0; i < GetSize(); i++) {
    if (!IsTombstoned(i)) {
      visible++;
    }
  }
  return visible;
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
