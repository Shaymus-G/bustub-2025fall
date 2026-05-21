//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.h
//
// Identification: src/include/storage/page/b_plus_tree_leaf_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {

#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>
#define LEAF_PAGE_HEADER_SIZE 16
#define LEAF_PAGE_DEFAULT_TOMB_CNT 0
#define LEAF_PAGE_TOMB_CNT ((NumTombs < 0) ? LEAF_PAGE_DEFAULT_TOMB_CNT : NumTombs)
#define LEAF_PAGE_SLOT_CNT                                                                               \
  ((BUSTUB_PAGE_SIZE - LEAF_PAGE_HEADER_SIZE - sizeof(size_t) - (LEAF_PAGE_TOMB_CNT * sizeof(size_t))) / \
   (sizeof(KeyType) + sizeof(ValueType)))  // NOLINT

/**
 * Store indexed key and record id(record id = page id combined with slot id,
 * see include/common/rid.h for detailed implementation) together within leaf
 * page. Only support unique key.
 *
 * Leaf pages also contain a fixed buffer of "tombstone" indexes for entries
 * that have been deleted.
 *
 * Leaf page format (keys are stored in order, tomb order is up to you):
 *  --------------------
 * | HEADER | TOMB_SIZE | (where TOMB_SIZE is num_tombstones_)
 *  --------------------
 *  -----------------------------------
 * | TOMB(0) | TOMB(1) | ... | TOMB(k) |
 *  -----------------------------------
 *  ---------------------------------
 * | KEY(1) | KEY(2) | ... | KEY(n) |
 *  ---------------------------------
 *  ---------------------------------
 * | RID(1) | RID(2) | ... | RID(n) |
 *  ---------------------------------
 *
 *  Header format (size in byte, 16 bytes in total):
 *  -----------------------------------------------
 * | PageType (4) | CurrentSize (4) | MaxSize (4) |
 *  -----------------------------------------------
 *  -----------------
 * | NextPageId (4) |
 *  -----------------
 */
FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class BPlusTreeLeafPage : public BPlusTreePage {
 public:
  // Delete all constructor / destructor to ensure memory safety
  BPlusTreeLeafPage() = delete;
  BPlusTreeLeafPage(const BPlusTreeLeafPage &other) = delete;

  void Init(int max_size = LEAF_PAGE_SLOT_CNT);

  auto GetTombstones() const -> std::vector<KeyType>;

  // Helper methods
  auto GetNextPageId() const -> page_id_t;
  void SetNextPageId(page_id_t next_page_id);
  auto KeyAt(int index) const -> KeyType;

  // 获取指定位置的 RID
  auto ValueAt(int index) const -> ValueType;
  // 返回指定位置 key 的常量引用，供 IndexIterator::operator* 返回稳定引用
  auto KeyAtRef(int index) const -> const KeyType &;
  // 返回指定位置 value 的常量引用，供 IndexIterator::operator* 返回稳定引用
  auto ValueAtRef(int index) const -> const ValueType &;
  // 设置指定位置的 key
  void SetKeyAt(int index, const KeyType &key);
  // 设置指定位置的 value
  void SetValueAt(int index, const ValueType &value);
  // 返回当前 tombstone buffer 中记录的删除项数量，用于判断逻辑删除是否已经缓存满
  auto GetNumTombstones() const -> size_t;
  // 返回当前 leaf page 的 tombstone buffer 最大容量
  auto GetMaxTombstones() const -> size_t;
  // 判断物理数组中的某个下标是否已经被 tombstone 标记删除
  auto IsTombstoned(int index) const -> bool;
  // 将某个物理下标加入 tombstone buffer，若 tombstone buffer 已满，则先物理删除最旧 tombstone
  void AddTombstone(int index);
  // 在指定位置插入一个 key/value，并维护已有 tombstone 下标
  void InsertAt(int index, const KeyType &key, const ValueType &value);
  // 物理删除指定位置的 key/value，并维护 tombstone buffer 中保存的下标
  void DeleteAt(int index);
  // 移除指定物理下标对应的 tombstone 标记，用于重新插入一个已经被逻辑删除的 key
  auto RemoveTombstoneForIndex(int index) -> bool;
  // 将当前 leaf 中所有 pending tombstone 物理应用掉，便于 split 前简化数组状态
  void ApplyAllTombstones();
  // 返回 tombstone buffer 当前保存的第 pos 个物理下标，用于 borrow/coalesce 时把 tombstone 从 source leaf 转移到 destination leaf
  auto TombstoneAt(size_t pos) const -> size_t;
  // 向 tombstone buffer 追加一个已经确定的物理下标，用于搬移已有 tombstone
  void AppendTombstone(size_t index);
  // 清空当前 leaf 的 tombstone buffer，用于 source leaf 被 coalesce 后避免残留状态干扰调试
  void ClearTombstones();
  // 按 key 查找当前物理下标并追加 tombstone
  void AddTombstoneByKey(const KeyType &key, const KeyComparator &comparator);
  // 返回当前 leaf 中对外可见的 entry 数量
  auto GetNumVisibleEntries() const -> int;

  /**
   * @brief for test only return a string representing all keys in
   * this leaf page formatted as "(tombkey1, tombkey2, ...|key1,key2,key3,...)"
   *
   * @return std::string
   */
  auto ToString() const -> std::string {
    std::string kstr = "(";
    bool first = true;

    auto tombs = GetTombstones();
    for (size_t i = 0; i < tombs.size(); i++) {
      kstr.append(std::to_string(tombs[i].ToString()));
      if ((i + 1) < tombs.size()) {
        kstr.append(",");
      }
    }

    kstr.append("|");

    for (int i = 0; i < GetSize(); i++) {
      KeyType key = KeyAt(i);
      if (first) {
        first = false;
      } else {
        kstr.append(",");
      }

      kstr.append(std::to_string(key.ToString()));
    }
    kstr.append(")");

    return kstr;
  }

 private:
  page_id_t next_page_id_;
  size_t num_tombstones_;
  // Fixed-size tombstone buffer (indexes into key_array_ / rid_array_).
  size_t tombstones_[LEAF_PAGE_TOMB_CNT];
  // Array members for page data.
  KeyType key_array_[LEAF_PAGE_SLOT_CNT];
  ValueType rid_array_[LEAF_PAGE_SLOT_CNT];
  // (Spring 2025) Feel free to add more fields and helper functions below if needed

  // 删除 tombstone buffer 中第 tombstone_pos 个记录
  void RemoveTombstoneAt(size_t tombstone_pos);
};

}  // namespace bustub
