//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.h
//
// Identification: src/include/storage/index/b_plus_tree.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * b_plus_tree.h
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */
#pragma once

#include <algorithm>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/macros.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

struct PrintableBPlusTree;

/**
 * @brief Definition of the Context class.
 *
 * Hint: This class is designed to help you keep track of the pages
 * that you're modifying or accessing.
 */
class Context {
 public:
  // When you insert into / remove from the B+ tree, store the write guard of header page here.
  // Remember to drop the header page guard and set it to nullopt when you want to unlock all.
  std::optional<WritePageGuard> header_page_{std::nullopt};

  // Save the root page id here so that it's easier to know if the current page is the root page.
  page_id_t root_page_id_{INVALID_PAGE_ID};

  // Store the write guards of the pages that you're modifying here.
  std::deque<WritePageGuard> write_set_;

  // You may want to use this when getting value, but not necessary.
  std::deque<ReadPageGuard> read_set_;

  auto IsRootPage(page_id_t page_id) -> bool { return page_id == root_page_id_; }
};

#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator, NumTombs>

// Main class providing the API for the Interactive B+ Tree.
FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
class BPlusTree {
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;

 public:
  explicit BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                     const KeyComparator &comparator, int leaf_max_size = LEAF_PAGE_SLOT_CNT,
                     int internal_max_size = INTERNAL_PAGE_SLOT_CNT);

  // Returns true if this B+ tree has no keys and values.
  auto IsEmpty() const -> bool;

  // Insert a key-value pair into this B+ tree.
  auto Insert(const KeyType &key, const ValueType &value) -> bool;

  // Remove a key and its value from this B+ tree.
  void Remove(const KeyType &key);

  // Return the value associated with a given key
  auto GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool;

  // Return the page id of the root node
  auto GetRootPageId() -> page_id_t;

  // Index iterator
  auto Begin() -> INDEXITERATOR_TYPE;

  auto End() -> INDEXITERATOR_TYPE;

  auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;

  void Print(BufferPoolManager *bpm);

  void Draw(BufferPoolManager *bpm, const std::filesystem::path &outf);

  auto DrawBPlusTree() -> std::string;

  // read data from file and insert one by one
  void InsertFromFile(const std::filesystem::path &file_name);

  // read data from file and remove one by one
  void RemoveFromFile(const std::filesystem::path &file_name);

  void BatchOpsFromFile(const std::filesystem::path &file_name);

  // Do not change this type to a BufferPoolManager!
  std::shared_ptr<TracedBufferPoolManager> bpm_;

 private:
  void ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out);

  void PrintTree(page_id_t page_id, const BPlusTreePage *page);

  auto ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree;

  // 在 leaf page 中二分查找第一个 >= key 的位置
  auto LeafLowerBound(const LeafPage *leaf_page, const KeyType &key) const -> int;

  // 在 internal page 中根据 separator key 找到应该继续访问的 child page_id
  auto InternalLookup(const InternalPage *internal_page, const KeyType &key) const -> page_id_t;

  // 以读模式从 root 到目标 leaf
  auto FindLeafPageRead(const KeyType &key, bool leftmost = false) -> std::optional<ReadPageGuard>;

  // 以写模式从 root 到目标 leaf，并把路径 guard 保存到 Context 中
  void FindLeafPageWrite(const KeyType &key, Context *ctx, bool leftmost = false);

  // 创建一个新 page，并返回新 page_id
  auto AllocateNewPageId() -> page_id_t;

  // leaf 满时先分裂 leaf，再把 key/value 插入到正确的 leaf 中
  void SplitLeafAndInsert(Context *ctx, const KeyType &key, const ValueType &value);

  // 将新 child 的 separator key 插入 parent；如果 parent 满，则递归分裂 internal page
  void InsertIntoParent(Context *ctx, page_id_t old_child_page_id, const KeyType &separator_key, page_id_t new_child_page_id);

  // 用给定的 key/value 数组重写 internal page，避免在满页上直接插入导致越界
  void RewriteInternalPage(InternalPage *page, const std::vector<KeyType> &keys, const std::vector<page_id_t> &values);
  
  // Optimistic 插入：先读到目标 leaf；如果 leaf 不会 split，则只写 leaf 完成插入
  // 返回 true 表示插入流程已经处理完；返回 false 表示 leaf 不安全，需要回退到保守写路径
  auto TryOptimisticInsert(const KeyType &key, const ValueType &value) -> std::optional<bool>;

  // 删除后 leaf underflow 时，尝试从兄弟节点借 entry；借不到则合并 leaf
  void RebalanceLeafAfterDelete(Context *ctx);

  // 从 internal parent 中移除第 remove_index 个 child 指针及对应 separator key
  void RemoveChildFromInternal(InternalPage *parent, int remove_index);

  // parent 是 root 且只剩一个 child 时，把唯一 child 提升为新 root
  void AdjustRootAfterDelete(Context *ctx);

  // Optimistic 删除：先读到目标 leaf；如果删除不会引起结构变化，则只写 leaf 完成删除
  // 返回 true 表示删除流程已经处理完；返回 false 表示需要回退到保守写路径
  auto TryOptimisticDelete(const KeyType &key) -> bool;

  // member variable
  std::string index_name_;
  KeyComparator comparator_;
  std::vector<std::string> log;  // NOLINT
  int leaf_max_size_;
  int internal_max_size_;
  page_id_t header_page_id_;
};

/**
 * @brief for test only. PrintableBPlusTree is a printable B+ tree.
 * We first convert B+ tree into a printable B+ tree and the print it.
 */
struct PrintableBPlusTree {
  int size_;
  std::string keys_;
  std::vector<PrintableBPlusTree> children_;

  /**
   * @brief BFS traverse a printable B+ tree and print it into
   * into out_buf
   *
   * @param out_buf
   */
  void Print(std::ostream &out_buf) {
    std::vector<PrintableBPlusTree *> que = {this};
    while (!que.empty()) {
      std::vector<PrintableBPlusTree *> new_que;

      for (auto &t : que) {
        int padding = (t->size_ - t->keys_.size()) / 2;
        out_buf << std::string(padding, ' ');
        out_buf << t->keys_;
        out_buf << std::string(padding, ' ');

        for (auto &c : t->children_) {
          new_que.push_back(&c);
        }
      }
      out_buf << "\n";
      que = new_que;
    }
  }
};

}  // namespace bustub
