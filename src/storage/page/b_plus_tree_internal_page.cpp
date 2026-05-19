//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * @brief Init method after creating a new internal page.
 *
 * Writes the necessary header information to a newly created page,
 * including set page type, set current size, set page id, set parent id and set max page size,
 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
 *
 * @param max_size Maximal size of the page
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  // 初始化公共 header
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
}

/**
 * @brief Helper method to get/set the key associated with input "index"(a.k.a
 * array offset).
 *
 * @param index The index of the key to get. Index must be non-zero.
 * @return Key at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  // KeyAt(0) 在语义上是 invalid key，但它仍然是数组中的合法位置，故这里允许 index == 0，查找逻辑中再主动跳过它
  assert(index >= 0);
  assert(index < GetSize());
  return key_array_[index];
}

/**
 * @brief Set key at the specified index.
 *
 * @param index The index of the key to set. Index must be non-zero.
 * @param key The new value for key
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  assert(index >= 0);
  assert(index < GetMaxSize());
  key_array_[index] = key;
}

/**
 * @brief Helper method to get the value associated with input "index"(a.k.a array
 * offset)
 *
 * @param index The index of the value to get.
 * @return Value at index
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  // internal page 的 value 是 child page_id，与 key 不同，ValueAt(0) 是有效的第一个孩子指针
  assert(index >= 0);
  assert(index < GetSize());
  return page_id_array_[index];
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  // SetValueAt 用于设置 child page_id
  assert(index >= 0);
  assert(index < GetMaxSize());
  page_id_array_[index] = value;
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  // 在 parent 中根据 child page_id 找孩子下标，找不到时返回 -1，由上层逻辑决定 assert 或直接处理异常情况。
  for (int i = 0; i < GetSize(); i++) {
    if (page_id_array_[i] == value) {
      return i;
    }
  }
  return -1;
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
