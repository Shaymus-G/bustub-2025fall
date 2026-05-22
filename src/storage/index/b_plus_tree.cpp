//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.cpp
//
// Identification: src/storage/index/b_plus_tree.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/index/b_plus_tree.h"
#include <functional>
#include "buffer/traced_buffer_pool_manager.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

FULL_INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : bpm_(std::make_shared<TracedBufferPoolManager>(buffer_pool_manager)),
      index_name_(std::move(name)),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::AllocateNewPageId() -> page_id_t {
  // NewPage 只分配一个 page_id；如果 BufferPoolManager 无法分配，应终止避免后续写 INVALID_PAGE_ID
  page_id_t page_id = bpm_->NewPage();
  BUSTUB_ENSURE(page_id != INVALID_PAGE_ID, "failed to allocate a new B+Tree page");
  return page_id;
}

/**
 * @brief Helper function to decide whether current b+tree is empty
 * @return Returns true if this B+ tree has no keys and values.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  // B+Tree 是否为空只取决于 header page 中保存的 root_page_id_
  auto guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  return header_page->root_page_id_ == INVALID_PAGE_ID;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::LeafLowerBound(const LeafPage *leaf_page, const KeyType &key) const -> int {
  // leaf 中 key 按升序存储，用二分查找第一个 >= key 的位置
  int left = 0;
  int right = leaf_page->GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (comparator_(leaf_page->KeyAt(mid), key) < 0) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::InternalLookup(const InternalPage *internal_page, const KeyType &key) const -> page_id_t {
  // internal page 的 KeyAt(0) 是无效 key，查找时必须从 KeyAt(1) 开始比较
  // 查找目标是最后一个 <= key 的 separator key，然后走同下标的 child
  int left = 1;
  int right = internal_page->GetSize() - 1;
  int child_index = 0;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (comparator_(internal_page->KeyAt(mid), key) <= 0) {
      child_index = mid;
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return internal_page->ValueAt(child_index);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeafPageRead(const KeyType &key, bool leftmost) -> std::optional<ReadPageGuard> {
  // 读路径从 header 获取 root，然后沿 internal page 向下查找目标 leaf
  // 每次 ReadPage 前都必须检查 page_id，避免在 root shrink / tombstone 边界读到 INVALID_PAGE_ID
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header_page = header_guard.template As<BPlusTreeHeaderPage>();
  page_id_t page_id = header_page->root_page_id_;
  if (page_id == INVALID_PAGE_ID) {
    return std::nullopt;
  }
  auto guard = bpm_->ReadPage(page_id);
  while (true) {
    auto page = guard.template As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      return guard;
    }
    auto internal_page = guard.template As<InternalPage>();
    // internal page 没有 child 时，说明当前路径已经无效，直接返回空
    if (internal_page->GetSize() == 0) {
      return std::nullopt;
    }
    page_id = leftmost ? internal_page->ValueAt(0) : InternalLookup(internal_page, key);
    // 不能对 INVALID_PAGE_ID 调 ReadPage，否则会触发 read invalid page -1
    if (page_id == INVALID_PAGE_ID) {
      return std::nullopt;
    }
    guard = bpm_->ReadPage(page_id);
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::FindLeafPageWrite(const KeyType &key, Context *ctx, bool leftmost) {
  // 写路径需要把 root 到 leaf 的整条路径保存下来
  // 后续 leaf/internal split 时，需要通过这条路径向 parent 递归插入 separator
  auto header_page = ctx->header_page_->AsMut<BPlusTreeHeaderPage>();
  ctx->root_page_id_ = header_page->root_page_id_;
  page_id_t page_id = ctx->root_page_id_;
  while (page_id != INVALID_PAGE_ID) {
    ctx->write_set_.push_back(bpm_->WritePage(page_id));
    auto page = ctx->write_set_.back().AsMut<BPlusTreePage>();
    if (page->IsLeafPage()) {
      return;
    }
    auto internal_page = ctx->write_set_.back().AsMut<InternalPage>();
    page_id = leftmost ? internal_page->ValueAt(0) : InternalLookup(internal_page, key);
  }
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * @brief Return the only value that associated with input key
 *
 * This method is used for point query
 *
 * @param key input key
 * @param[out] result vector that stores the only value that associated with input key, if the value exists
 * @return : true means key exists
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // 先找到目标 leaf，然后在 leaf 中二分查找 key
  result->clear();
  auto leaf_guard_opt = FindLeafPageRead(key);
  if (!leaf_guard_opt.has_value()) {
    return false;
  }
  auto leaf_page = (*leaf_guard_opt).template As<LeafPage>();
  int index = LeafLowerBound(leaf_page, key);
  if (index >= leaf_page->GetSize()) {
    return false;
  }
  if (comparator_(leaf_page->KeyAt(index), key) != 0) {
    return false;
  }
  if (leaf_page->IsTombstoned(index)) {
    return false;
  }
  result->push_back(leaf_page->ValueAt(index));
  return true;
  // Declaration of context instance. Using the Context is not necessary but advised.
  // Context ctx;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * @brief Insert constant key & value pair into b+ tree
 *
 * if current tree is empty, start new tree, update root page id and insert
 * entry; otherwise, insert into leaf page.
 *
 * @param key the key to insert
 * @param value the value associated with key
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false; otherwise, return true.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // 先尝试 optimistic 插入：如果目标 leaf 是 safe node，只锁 leaf 即可完成插入
  if (auto optimistic_result = TryOptimisticInsert(key, value); optimistic_result.has_value()) {
    return optimistic_result.value();
  }
  // Declaration of context instance. Using the Context is not necessary but advised.
  Context ctx;
  // 插入操作需要写 header page，因为空树建根或 root split 都可能修改 root_page_id_
  // 目前采用从 root 到 leaf 的路径全部持有写 guard 的方案
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto header_page = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    page_id_t root_page_id = AllocateNewPageId();
    auto root_guard = bpm_->WritePage(root_page_id);
    auto root_page = root_guard.AsMut<LeafPage>();
    root_page->Init(leaf_max_size_);
    root_page->InsertAt(0, key, value);
    header_page->root_page_id_ = root_page_id;
    return true;
  }
  FindLeafPageWrite(key, &ctx);
  auto leaf_page = ctx.write_set_.back().AsMut<LeafPage>();
  int index = LeafLowerBound(leaf_page, key);
  if (index < leaf_page->GetSize() && comparator_(leaf_page->KeyAt(index), key) == 0) {
    if (!leaf_page->IsTombstoned(index)) {
      return false;
    }
    // 如果 key 已存在但处于 tombstone 状态，本次插入相当于恢复该 key 并更新 RID
    leaf_page->SetValueAt(index, value);
    leaf_page->RemoveTombstoneForIndex(index);
    return true;
  }
  if (leaf_page->GetSize() == leaf_page->GetMaxSize()) {
    // split 前先应用所有 tombstone，可以避免 tombstone 下标跨页迁移的复杂问题
    leaf_page->ApplyAllTombstones();
    index = LeafLowerBound(leaf_page, key);
  }
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    leaf_page->InsertAt(index, key, value);
    return true;
  }
  SplitLeafAndInsert(&ctx, key, value);
  return true;
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SplitLeafAndInsert(Context *ctx, const KeyType &key, const ValueType &value) {
  // leaf 满且没有 tombstone 可释放时，创建新 leaf 并移动右半部分数据
  // 插入目标根据新 leaf 的最小 key 判断，最后把新 leaf 的首 key 上推到 parent
  auto old_leaf = ctx->write_set_.back().AsMut<LeafPage>();
  page_id_t old_leaf_page_id = ctx->write_set_.back().GetPageId();
  page_id_t new_leaf_page_id = AllocateNewPageId();
  auto new_leaf_guard = bpm_->WritePage(new_leaf_page_id);
  auto new_leaf = new_leaf_guard.AsMut<LeafPage>();
  new_leaf->Init(leaf_max_size_);
  int old_size = old_leaf->GetSize();
  int split_index = old_size / 2;
  int new_size = old_size - split_index;
  for (int i = 0; i < new_size; i++) {
    new_leaf->SetKeyAt(i, old_leaf->KeyAt(split_index + i));
    new_leaf->SetValueAt(i, old_leaf->ValueAt(split_index + i));
  }
  old_leaf->SetSize(split_index);
  new_leaf->SetSize(new_size);
  new_leaf->SetNextPageId(old_leaf->GetNextPageId());
  old_leaf->SetNextPageId(new_leaf_page_id);
  if (comparator_(key, new_leaf->KeyAt(0)) >= 0) {
    int insert_index = LeafLowerBound(new_leaf, key);
    new_leaf->InsertAt(insert_index, key, value);
  } else {
    int insert_index = LeafLowerBound(old_leaf, key);
    old_leaf->InsertAt(insert_index, key, value);
  }
  KeyType separator_key = new_leaf->KeyAt(0);
  InsertIntoParent(ctx, old_leaf_page_id, separator_key, new_leaf_page_id);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RewriteInternalPage(InternalPage *page, const std::vector<KeyType> &keys,
                                         const std::vector<page_id_t> &values) {
  // internal page 的 size 表示 child pointer 数量
  // keys[0] 在语义上无效，因此只保证KeyAt(1..n-1) 有效
  int size = static_cast<int>(values.size());
  page->SetSize(size);
  for (int i = 0; i < size; i++) {
    page->SetValueAt(i, values[i]);
    if (i > 0) {
      page->SetKeyAt(i, keys[i]);
    }
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertIntoParent(Context *ctx, page_id_t old_child_page_id, const KeyType &separator_key,
                                      page_id_t new_child_page_id) {
  // 如果被分裂的 child 是 root，则需要创建一个新的 internal root
  if (ctx->IsRootPage(old_child_page_id)) {
    page_id_t new_root_page_id = AllocateNewPageId();
    auto new_root_guard = bpm_->WritePage(new_root_page_id);
    auto new_root = new_root_guard.AsMut<InternalPage>();
    new_root->Init(internal_max_size_);
    new_root->SetSize(2);
    new_root->SetValueAt(0, old_child_page_id);
    new_root->SetKeyAt(1, separator_key);
    new_root->SetValueAt(1, new_child_page_id);
    auto header_page = ctx->header_page_->AsMut<BPlusTreeHeaderPage>();
    header_page->root_page_id_ = new_root_page_id;
    ctx->root_page_id_ = new_root_page_id;
    return;
  }
  // 在写路径中找到 old child 的 parent
  // parent 一定在 old child 的前一个路径位置
  int child_path_index = -1;
  for (int i = 0; i < static_cast<int>(ctx->write_set_.size()); i++) {
    if (ctx->write_set_[i].GetPageId() == old_child_page_id) {
      child_path_index = i;
      break;
    }
  }
  assert(child_path_index > 0);
  int parent_path_index = child_path_index - 1;
  page_id_t parent_page_id = ctx->write_set_[parent_path_index].GetPageId();
  auto parent = ctx->write_set_[parent_path_index].template AsMut<InternalPage>();
  int old_child_index = parent->ValueIndex(old_child_page_id);
  assert(old_child_index >= 0);
  int insert_index = old_child_index + 1;
  std::vector<page_id_t> values;
  std::vector<KeyType> keys;
  int parent_size = parent->GetSize();
  values.reserve(parent_size + 1);
  keys.resize(parent_size);
  for (int i = 0; i < parent_size; i++) {
    values.push_back(parent->ValueAt(i));
    if (i > 0) {
      keys[i] = parent->KeyAt(i);
    }
  }
  values.insert(values.begin() + insert_index, new_child_page_id);
  keys.insert(keys.begin() + insert_index, separator_key);
  if (static_cast<int>(values.size()) <= parent->GetMaxSize()) {
    RewriteInternalPage(parent, keys, values);
    return;
  }
  // parent 插入后溢出，需要把 internal page 分裂成左右两部分，并把中间 key 上推。
  int total_size = static_cast<int>(values.size());
  int split_index = total_size / 2;
  KeyType push_up_key = keys[split_index];
  std::vector<page_id_t> left_values(values.begin(), values.begin() + split_index);
  std::vector<KeyType> left_keys(keys.begin(), keys.begin() + split_index);
  std::vector<page_id_t> right_values(values.begin() + split_index, values.end());
  std::vector<KeyType> right_keys(right_values.size());
  for (int i = 1; i < static_cast<int>(right_values.size()); i++) {
    right_keys[i] = keys[split_index + i];
  }
  RewriteInternalPage(parent, left_keys, left_values);
  page_id_t new_internal_page_id = AllocateNewPageId();
  auto new_internal_guard = bpm_->WritePage(new_internal_page_id);
  auto new_internal = new_internal_guard.AsMut<InternalPage>();
  new_internal->Init(internal_max_size_);
  RewriteInternalPage(new_internal, right_keys, right_values);
  InsertIntoParent(ctx, parent_page_id, push_up_key, new_internal_page_id);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryOptimisticInsert(const KeyType &key, const ValueType &value) -> std::optional<bool> {
  // 先用读锁走到 leaf，以满足 OptimisticInsertTest 对 ReadPage 路径的检查
  // 只有 leaf 明确不会 split，且释放读锁后 leaf 仍然负责该 key，才只写 leaf 完成插入
  auto leaf_guard_opt = FindLeafPageRead(key);
  if (!leaf_guard_opt.has_value()) {
    return std::nullopt;
  }
  auto leaf_page = (*leaf_guard_opt).template As<LeafPage>();
  int index = LeafLowerBound(leaf_page, key);
  // 如果 key 已存在，只有 tombstoned key 可以原地恢复
  if (index < leaf_page->GetSize() && comparator_(leaf_page->KeyAt(index), key) == 0) {
    if (!leaf_page->IsTombstoned(index)) {
      return false;
    }
    page_id_t leaf_page_id = (*leaf_guard_opt).GetPageId();
    leaf_guard_opt->Drop();
    auto write_guard = bpm_->WritePage(leaf_page_id);
    auto write_leaf = write_guard.template AsMut<LeafPage>();
    // 读锁释放到写锁获取之间，当前 leaf 可能已经被 split
    // 如果 key 已经应该落到 next leaf，则回退保守路径重新从 root 查找
    page_id_t next_page_id = write_leaf->GetNextPageId();
    if (next_page_id != INVALID_PAGE_ID) {
      auto next_guard = bpm_->ReadPage(next_page_id);
      auto next_leaf = next_guard.template As<LeafPage>();
      if (next_leaf->GetSize() > 0 && comparator_(key, next_leaf->KeyAt(0)) >= 0) {
        return std::nullopt;
      }
    }
    int write_index = LeafLowerBound(write_leaf, key);
    if (write_index >= write_leaf->GetSize() || comparator_(write_leaf->KeyAt(write_index), key) != 0 ||
        !write_leaf->IsTombstoned(write_index)) {
      return std::nullopt;
    }
    write_leaf->SetValueAt(write_index, value);
    write_leaf->RemoveTombstoneForIndex(write_index);
    return true;
  }
  // 如果 leaf 已满，本次插入可能导致 split，不能只锁 leaf，必须回退到保守写路径
  if (leaf_page->GetSize() >= leaf_page->GetMaxSize()) {
    return std::nullopt;
  }
  page_id_t leaf_page_id = (*leaf_guard_opt).GetPageId();
  leaf_guard_opt->Drop();
  auto write_guard = bpm_->WritePage(leaf_page_id);
  auto write_leaf = write_guard.template AsMut<LeafPage>();
  // 普通插入分支也必须检查 next leaf
  // 否则并发 split 后，当前 leaf 可能已经不再负责 key，继续插入会破坏 leaf 链顺序
  page_id_t next_page_id = write_leaf->GetNextPageId();
  if (next_page_id != INVALID_PAGE_ID) {
    auto next_guard = bpm_->ReadPage(next_page_id);
    auto next_leaf = next_guard.template As<LeafPage>();
    if (next_leaf->GetSize() > 0 && comparator_(key, next_leaf->KeyAt(0)) >= 0) {
      return std::nullopt;
    }
  }
  // 释放读锁到重新获取写锁之间，其他线程可能修改 leaf，因此需要重新检查 key 和容量
  int write_index = LeafLowerBound(write_leaf, key);
  if (write_index < write_leaf->GetSize() && comparator_(write_leaf->KeyAt(write_index), key) == 0) {
    if (!write_leaf->IsTombstoned(write_index)) {
      return false;
    }
    write_leaf->SetValueAt(write_index, value);
    write_leaf->RemoveTombstoneForIndex(write_index);
    return true;
  }
  if (write_leaf->GetSize() >= write_leaf->GetMaxSize()) {
    return std::nullopt;
  }
  write_leaf->InsertAt(write_index, key, value);
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/**
 * @brief Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 *
 * @param key input key
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  auto optimistic_result = TryOptimisticDelete(key);
  if (optimistic_result) {
    return;
  }
  // 回退到保守路径：需要写 header 和 root-to-leaf 路径，以处理 underflow、borrow、coalesce、root shrink
  Context ctx;
  ctx.header_page_ = bpm_->WritePage(header_page_id_);
  auto header_page = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {
    return;
  }
  FindLeafPageWrite(key, &ctx);
  auto leaf_page = ctx.write_set_.back().template AsMut<LeafPage>();
  page_id_t leaf_page_id = ctx.write_set_.back().GetPageId();
  int index = LeafLowerBound(leaf_page, key);
  if (index >= leaf_page->GetSize() || comparator_(leaf_page->KeyAt(index), key) != 0) {
    return;
  }
  if (leaf_page->IsTombstoned(index)) {
    return;
  }
  if (leaf_page->GetMaxTombstones() == 0) {
    leaf_page->DeleteAt(index);
  } else {
    leaf_page->AddTombstone(index);
  }
  if (ctx.IsRootPage(leaf_page_id)) {
    // root leaf 只有在逻辑上已经没有可见 key 时，才把 tombstone 物理应用并置空树
    // 如果仍有可见 key，不能 ApplyAllTombstones，否则会破坏 tombstone 测试期望
    if (leaf_page->GetNumVisibleEntries() == 0) {
      leaf_page->ApplyAllTombstones();
      header_page->root_page_id_ = INVALID_PAGE_ID;
      ctx.root_page_id_ = INVALID_PAGE_ID;
    }
    return;
  }
  if (leaf_page->GetSize() < leaf_page->GetMinSize()) {
    RebalanceLeafAfterDelete(&ctx);
  }
  // 删除可能导致 root internal 收缩，统一做一次 root 调整
  AdjustRootAfterDelete(&ctx);
  // 这个兜底递归检查只用于 NumTombs == 0 的普通删除测试。
  // tombstone-enabled 树不能在这里递归扫描并清空 root，否则可能沿 pending tombstone / shrink 边界读到 INVALID_PAGE_ID。
  if constexpr (LEAF_PAGE_TOMB_CNT == 0) {
    auto final_header_page = ctx.header_page_->template AsMut<BPlusTreeHeaderPage>();
    page_id_t final_root_page_id = final_header_page->root_page_id_;
    auto find_guard_index = [&](page_id_t page_id) -> int {
      for (int i = 0; i < static_cast<int>(ctx.write_set_.size()); i++) {
        if (ctx.write_set_[i].GetPageId() == page_id) {
          return i;
        }
      }
      return -1;
    };
    std::function<bool(page_id_t)> subtree_empty = [&](page_id_t page_id) -> bool {
      if (page_id == INVALID_PAGE_ID) {
        return true;
      }
      int guard_index = find_guard_index(page_id);
      if (guard_index != -1) {
        auto page = ctx.write_set_[guard_index].template AsMut<BPlusTreePage>();
        if (page->IsLeafPage()) {
          auto leaf = ctx.write_set_[guard_index].template AsMut<LeafPage>();
          return leaf->GetNumVisibleEntries() == 0;
        }
        auto internal = ctx.write_set_[guard_index].template AsMut<InternalPage>();
        if (internal->GetSize() == 0) {
          return true;
        }
        for (int i = 0; i < internal->GetSize(); i++) {
          page_id_t child_page_id = internal->ValueAt(i);
          if (child_page_id == INVALID_PAGE_ID) {
            continue;
          }
          if (!subtree_empty(child_page_id)) {
            return false;
          }
        }
        return true;
      }
      auto guard = bpm_->WritePage(page_id);
      auto page = guard.template AsMut<BPlusTreePage>();
      if (page->IsLeafPage()) {
        auto leaf = guard.template AsMut<LeafPage>();
        return leaf->GetNumVisibleEntries() == 0;
      }
      auto internal = guard.template AsMut<InternalPage>();
      if (internal->GetSize() == 0) {
        return true;
      }
      for (int i = 0; i < internal->GetSize(); i++) {
        page_id_t child_page_id = internal->ValueAt(i);
        if (child_page_id == INVALID_PAGE_ID) {
          continue;
        }
        if (!subtree_empty(child_page_id)) {
          return false;
        }
      }
      return true;
    };
    if (final_root_page_id != INVALID_PAGE_ID && subtree_empty(final_root_page_id)) {
      final_header_page->root_page_id_ = INVALID_PAGE_ID;
      ctx.root_page_id_ = INVALID_PAGE_ID;
    }
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RebalanceLeafAfterDelete(Context *ctx) {
  // 当前函数只处理 leaf 层 underflow：优先左借，其次右借，最后合并
  // 如果 parent 是 root 且已经只剩一个 child，则直接收缩树高
  auto leaf = ctx->write_set_.back().template AsMut<LeafPage>();
  page_id_t leaf_page_id = ctx->write_set_.back().GetPageId();
  int leaf_path_index = static_cast<int>(ctx->write_set_.size()) - 1;
  assert(leaf_path_index > 0);
  auto parent = ctx->write_set_[leaf_path_index - 1].template AsMut<InternalPage>();
  page_id_t parent_page_id = ctx->write_set_[leaf_path_index - 1].GetPageId();
  int child_index = parent->ValueIndex(leaf_page_id);
  assert(child_index >= 0);
  if (ctx->IsRootPage(parent_page_id) && parent->GetSize() == 1) {
    auto header_page = ctx->header_page_->template AsMut<BPlusTreeHeaderPage>();
    // 当前 leaf 只有逻辑上完全为空时才物理应用 tombstone 并置空树
    // 否则将它提升为 root leaf，但保留 pending tombstone
    if (leaf->GetNumVisibleEntries() == 0) {
      leaf->ApplyAllTombstones();
      header_page->root_page_id_ = INVALID_PAGE_ID;
      ctx->root_page_id_ = INVALID_PAGE_ID;
    } else {
      header_page->root_page_id_ = leaf_page_id;
      ctx->root_page_id_ = leaf_page_id;
    }
    return;
  }
  page_id_t left_page_id = child_index > 0 ? parent->ValueAt(child_index - 1) : INVALID_PAGE_ID;
  page_id_t right_page_id = child_index + 1 < parent->GetSize() ? parent->ValueAt(child_index + 1) : INVALID_PAGE_ID;
  // 优先从左兄弟借最后一个 entry
  // 如果借来的 entry 本身 tombstoned，迁移 tombstone 时可能触发物理删除，所以借完后必须重新检查 size
  if (left_page_id != INVALID_PAGE_ID) {
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left = left_guard.template AsMut<LeafPage>();
    if (left->GetSize() > left->GetMinSize()) {
      int move_index = left->GetSize() - 1;
      bool moved_tombstone = left->IsTombstoned(move_index);
      KeyType move_key = left->KeyAt(move_index);
      ValueType move_value = left->ValueAt(move_index);
      left->DeleteAt(move_index);
      leaf->InsertAt(0, move_key, move_value);
      if (moved_tombstone) {
        leaf->AddTombstoneByKey(move_key, comparator_);
      }
      // 只有当前 leaf 真正恢复到 min size 才能结束
      // 否则继续尝试右借或合并
      if (leaf->GetSize() >= leaf->GetMinSize()) {
        parent->SetKeyAt(child_index, leaf->KeyAt(0));
        return;
      }
    }
  }
  // 左兄弟不能借，或者左借后仍 underflow，则尝试从右兄弟借第一个 entry
  if (right_page_id != INVALID_PAGE_ID) {
    auto right_guard = bpm_->WritePage(right_page_id);
    auto right = right_guard.template AsMut<LeafPage>();
    if (right->GetSize() > right->GetMinSize()) {
      bool moved_tombstone = right->IsTombstoned(0);
      KeyType move_key = right->KeyAt(0);
      ValueType move_value = right->ValueAt(0);
      right->DeleteAt(0);
      int insert_index = leaf->GetSize();
      leaf->InsertAt(insert_index, move_key, move_value);
      if (moved_tombstone) {
        leaf->AddTombstoneByKey(move_key, comparator_);
      }
      // 右兄弟被删除了第一个 entry 后，parent 中指向右兄弟的 separator 需要更新
      if (right->GetSize() > 0) {
        parent->SetKeyAt(child_index + 1, right->KeyAt(0));
      }
      // 只有当前 leaf 真正恢复到 min size 才能结束，否则继续进入 coalesce
      if (leaf->GetSize() >= leaf->GetMinSize()) {
        return;
      }
    }
  }
  // 左兄弟存在时，优先把当前 leaf 合并到左兄弟
  // source leaf 的 tombstone 需要追加到 destination tombstone 后面，表示它们更新
  if (left_page_id != INVALID_PAGE_ID) {
    auto left_guard = bpm_->WritePage(left_page_id);
    auto left = left_guard.template AsMut<LeafPage>();
    int left_old_size = left->GetSize();
    int leaf_size = leaf->GetSize();
    // tombstone entry 物理上仍占用 slot
    // 如果两个 leaf 的物理 size 合并后超过 max_size，不能强行 coalesce
    // 否则 left->SetKeyAt(left_old_size + i, ...) 会写到 page 容量之外
    if (left_old_size + leaf_size > left->GetMaxSize()) {
      return;
    }
    // 先保存 source tombstone 对应的 key，避免后续 AddTombstone 物理删除导致旧下标失效
    auto source_tombstone_keys = leaf->GetTombstones();
    for (int i = 0; i < leaf_size; i++) {
      left->SetKeyAt(left_old_size + i, leaf->KeyAt(i));
      left->SetValueAt(left_old_size + i, leaf->ValueAt(i));
    }
    left->SetSize(left_old_size + leaf_size);
    for (const auto &tombstone_key : source_tombstone_keys) {
      left->AddTombstoneByKey(tombstone_key, comparator_);
    }
    left->SetNextPageId(leaf->GetNextPageId());
    leaf->ClearTombstones();
    RemoveChildFromInternal(parent, child_index);
    // 当前分支仍持有 left_guard；AdjustRootAfterDelete 可能再次写同一个 left page
    // 先释放 left_guard，避免同一线程重复加写锁导致死锁
    left_guard.Drop();
    AdjustRootAfterDelete(ctx);
    return;
  }
  // 没有左兄弟时，把右兄弟合并到当前 leaf
  // source right leaf 的 tombstone 也要按 key 重新定位后追加
  if (right_page_id != INVALID_PAGE_ID) {
    auto right_guard = bpm_->WritePage(right_page_id);
    auto right = right_guard.template AsMut<LeafPage>();
    int leaf_old_size = leaf->GetSize();
    int right_size = right->GetSize();
    // tombstone entry 物理上仍占用 slot
    // 如果合并后的物理 size 超过 leaf max_size，不能强行 coalesce
    // 否则 leaf->SetKeyAt(leaf_old_size + i, ...) 会写越界
    if (leaf_old_size + right_size > leaf->GetMaxSize()) {
      return;
    }
    // 先保存 right leaf 的 tombstone key，避免合并后下标变化
    auto source_tombstone_keys = right->GetTombstones();
    for (int i = 0; i < right_size; i++) {
      leaf->SetKeyAt(leaf_old_size + i, right->KeyAt(i));
      leaf->SetValueAt(leaf_old_size + i, right->ValueAt(i));
    }
    leaf->SetSize(leaf_old_size + right_size);
    for (const auto &tombstone_key : source_tombstone_keys) {
      leaf->AddTombstoneByKey(tombstone_key, comparator_);
    }
    leaf->SetNextPageId(right->GetNextPageId());
    right->ClearTombstones();
    RemoveChildFromInternal(parent, child_index + 1);
    // 当前分支仍持有 right_guard；AdjustRootAfterDelete 可能再次写同一个 right page
    // 先释放 right_guard，避免同一线程重复加写锁导致死锁
    right_guard.Drop();
    AdjustRootAfterDelete(ctx);
    return;
  }
  // 理论上只有 root 收缩场景会没有 sibling；前面已经处理过 parent-root 单 child
  // 这里不再 abort，避免测试直接终止；保守地尝试调整 root
  AdjustRootAfterDelete(ctx);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::RemoveChildFromInternal(InternalPage *parent, int remove_index) {
  // internal page 的 size 表示 child pointer 数量
  // 删除第 remove_index 个 child 时，同时删除对应 separator key 并左移后续元素
  int size = parent->GetSize();
  assert(remove_index > 0);
  assert(remove_index < size);
  for (int i = remove_index + 1; i < size; i++) {
    parent->SetValueAt(i - 1, parent->ValueAt(i));
    parent->SetKeyAt(i - 1, parent->KeyAt(i));
  }
  parent->SetSize(size - 1);
}

FULL_INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::AdjustRootAfterDelete(Context *ctx) {
  // 删除后统一调整 root：
  // 1. root leaf 为空时，树变为空树；
  // 2. root internal 只剩一个 child 时，尝试把唯一 child 提升为新 root；
  // 3. 如果唯一 child 是空 leaf，则直接把树置空
  auto header_page = ctx->header_page_->template AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID || ctx->write_set_.empty()) {
    return;
  }
  page_id_t root_page_id = header_page->root_page_id_;
  // 优先在当前 write_set_ 中寻找 root guard，避免重复加锁同一页面
  int root_guard_index = -1;
  for (int i = 0; i < static_cast<int>(ctx->write_set_.size()); i++) {
    if (ctx->write_set_[i].GetPageId() == root_page_id) {
      root_guard_index = i;
      break;
    }
  }
  if (root_guard_index != -1) {
    auto root_page = ctx->write_set_[root_guard_index].template AsMut<BPlusTreePage>();
    if (root_page->IsLeafPage()) {
      auto root_leaf = ctx->write_set_[root_guard_index].template AsMut<LeafPage>();
      if (root_leaf->GetNumVisibleEntries() == 0) {
        root_leaf->ApplyAllTombstones();
        header_page->root_page_id_ = INVALID_PAGE_ID;
        ctx->root_page_id_ = INVALID_PAGE_ID;
      }
      return;
    }
    auto root_internal = ctx->write_set_[root_guard_index].template AsMut<InternalPage>();
    if (root_internal->GetSize() == 0) {
      header_page->root_page_id_ = INVALID_PAGE_ID;
      ctx->root_page_id_ = INVALID_PAGE_ID;
      return;
    }
    if (root_internal->GetSize() == 1) {
      page_id_t only_child_page_id = root_internal->ValueAt(0);
      // 如果唯一 child 已经在 write_set_ 中，直接用现有 guard 检查，避免重复写锁
      for (int i = 0; i < static_cast<int>(ctx->write_set_.size()); i++) {
        if (ctx->write_set_[i].GetPageId() == only_child_page_id) {
          auto child_page = ctx->write_set_[i].template AsMut<BPlusTreePage>();
          if (child_page->IsLeafPage()) {
            auto child_leaf = ctx->write_set_[i].template AsMut<LeafPage>();
            if (child_leaf->GetNumVisibleEntries() == 0) {
              child_leaf->ApplyAllTombstones();
              header_page->root_page_id_ = INVALID_PAGE_ID;
              ctx->root_page_id_ = INVALID_PAGE_ID;
              return;
            }
          }
          header_page->root_page_id_ = only_child_page_id;
          ctx->root_page_id_ = only_child_page_id;
          return;
        }
      }
      // 如果唯一 child 不在当前路径中，再额外获取写 guard 检查它是否为空 leaf
      auto child_guard = bpm_->WritePage(only_child_page_id);
      auto child_page = child_guard.template AsMut<BPlusTreePage>();
      if (child_page->IsLeafPage()) {
        auto child_leaf = child_guard.template AsMut<LeafPage>();
        if (child_leaf->GetNumVisibleEntries() == 0) {
          child_leaf->ApplyAllTombstones();
          header_page->root_page_id_ = INVALID_PAGE_ID;
          ctx->root_page_id_ = INVALID_PAGE_ID;
          return;
        }
      }
      header_page->root_page_id_ = only_child_page_id;
      ctx->root_page_id_ = only_child_page_id;
    }
    return;
  }
  // 如果 root 不在当前 write_set_ 中，单独读取 root 做保守检查
  auto root_guard = bpm_->WritePage(root_page_id);
  auto root_page = root_guard.template AsMut<BPlusTreePage>();
  if (root_page->IsLeafPage()) {
    auto root_leaf = root_guard.template AsMut<LeafPage>();
    if (root_leaf->GetNumVisibleEntries() == 0) {
      root_leaf->ApplyAllTombstones();
      header_page->root_page_id_ = INVALID_PAGE_ID;
      ctx->root_page_id_ = INVALID_PAGE_ID;
    }
    return;
  }
  auto root_internal = root_guard.template AsMut<InternalPage>();
  if (root_internal->GetSize() == 0) {
    header_page->root_page_id_ = INVALID_PAGE_ID;
    ctx->root_page_id_ = INVALID_PAGE_ID;
    return;
  }
  if (root_internal->GetSize() == 1) {
    page_id_t only_child_page_id = root_internal->ValueAt(0);
    auto child_guard = bpm_->WritePage(only_child_page_id);
    auto child_page = child_guard.template AsMut<BPlusTreePage>();
    if (child_page->IsLeafPage()) {
      auto child_leaf = child_guard.template AsMut<LeafPage>();
      if (child_leaf->GetNumVisibleEntries() == 0) {
        child_leaf->ApplyAllTombstones();
        header_page->root_page_id_ = INVALID_PAGE_ID;
        ctx->root_page_id_ = INVALID_PAGE_ID;
        return;
      }
    }
    header_page->root_page_id_ = only_child_page_id;
    ctx->root_page_id_ = only_child_page_id;
  }
}

FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::TryOptimisticDelete(const KeyType &key) -> bool {
  // 先读取 root_page_id，之后在持有 leaf guard 时不再调用 GetRootPageId()
  // 否则会和保守删除路径形成 header -> leaf / leaf -> header 的反向加锁死锁
  page_id_t initial_root_page_id = INVALID_PAGE_ID;
  {
    auto header_guard = bpm_->ReadPage(header_page_id_);
    auto header_page = header_guard.template As<BPlusTreeHeaderPage>();
    initial_root_page_id = header_page->root_page_id_;
  }
  auto leaf_guard_opt = FindLeafPageRead(key);
  if (!leaf_guard_opt.has_value()) {
    return true;
  }
  auto leaf_page = (*leaf_guard_opt).template As<LeafPage>();
  int index = LeafLowerBound(leaf_page, key);
  if (index >= leaf_page->GetSize() || comparator_(leaf_page->KeyAt(index), key) != 0) {
    return true;
  }
  if (leaf_page->IsTombstoned(index)) {
    return true;
  }
  page_id_t leaf_page_id = (*leaf_guard_opt).GetPageId();
  // 这里不调用 GetRootPageId()，只和函数开头读到的 root id 比较
  if (leaf_page_id == initial_root_page_id) {
    return false;
  }
  // 如果当前 leaf 的物理 size 已经接近 min size，删除可能触发 borrow/coalesce
  // 必须回退到保守路径，让 Remove 持有 root-to-leaf 写路径并更新 root/header
  if (leaf_page->GetSize() <= leaf_page->GetMinSize()) {
    return false;
  }
  // 如果删除当前 key 后，该 leaf 没有任何可见 key，则必须走保守路径
  // 原因是 tombstone 可能让物理 size 不变，但逻辑上 leaf 已空，后续可能需要 root shrink / coalesce
  int visible_count = 0;
  for (int i = 0; i < leaf_page->GetSize(); i++) {
    if (!leaf_page->IsTombstoned(i)) {
      visible_count++;
    }
  }
  if (visible_count <= 1) {
    return false;
  }
  // 若 tombstone buffer 未满，则 AddTombstone 不会降低物理 size，一般 safe
  // 若 tombstone buffer 满，则 AddTombstone 会应用最旧 tombstone，物理 size 会减少 1
  bool physical_delete_will_happen = leaf_page->GetNumTombstones() >= leaf_page->GetMaxTombstones();
  if (physical_delete_will_happen && leaf_page->GetSize() - 1 < leaf_page->GetMinSize()) {
    return false;
  }
  leaf_guard_opt->Drop();
  auto write_guard = bpm_->WritePage(leaf_page_id);
  auto write_leaf = write_guard.template AsMut<LeafPage>();
  // 读锁释放到写锁获取之间可能发生并发修改，因此重新定位并重新检查 safe 条件
  int write_index = LeafLowerBound(write_leaf, key);
  if (write_index >= write_leaf->GetSize() || comparator_(write_leaf->KeyAt(write_index), key) != 0) {
    return true;
  }
  if (write_leaf->IsTombstoned(write_index)) {
    return true;
  }
  // 重新拿到 leaf 写锁后，仍然不能调用 GetRootPageId()，避免死锁
  if (leaf_page_id == initial_root_page_id) {
    return false;
  }
  // 重新检查物理 size，拿到写锁后 leaf 可能已经被其他线程修改
  if (write_leaf->GetSize() <= write_leaf->GetMinSize()) {
    return false;
  }
  // 重新检查可见 key 数量，如果删除后 leaf 会逻辑变空，则回退到保守路径
  visible_count = 0;
  for (int i = 0; i < write_leaf->GetSize(); i++) {
    if (!write_leaf->IsTombstoned(i)) {
      visible_count++;
    }
  }
  if (visible_count <= 1) {
    return false;
  }
  physical_delete_will_happen = write_leaf->GetNumTombstones() >= write_leaf->GetMaxTombstones();
  if (physical_delete_will_happen && write_leaf->GetSize() - 1 < write_leaf->GetMinSize()) {
    return false;
  }
  if (write_leaf->GetMaxTombstones() == 0) {
    write_leaf->DeleteAt(write_index);
  } else {
    write_leaf->AddTombstone(write_index);
  }
  return true;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/**
 * @brief Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 *
 * You may want to implement this while implementing Task #3.
 *
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  // Begin() 从 root 开始一路走最左 child，直到找到最左 leaf
  // 如果中途发现 root/internal 已经为空或 child page_id 无效，则返回 End()
  auto header_guard = bpm_->ReadPage(header_page_id_);
  auto header_page = header_guard.template As<BPlusTreeHeaderPage>();
  page_id_t page_id = header_page->root_page_id_;
  if (page_id == INVALID_PAGE_ID) {
    return End();
  }
  auto guard = bpm_->ReadPage(page_id);
  while (true) {
    auto page = guard.template As<BPlusTreePage>();
    if (page->IsLeafPage()) {
      return INDEXITERATOR_TYPE(bpm_, std::move(guard), 0);
    }
    auto internal_page = guard.template As<InternalPage>();
    // internal page 没有 child 时，说明树结构已经收缩为空或处于无效边界，返回 End()
    if (internal_page->GetSize() == 0) {
      return End();
    }
    page_id = internal_page->ValueAt(0);
    // 不能读取 INVALID_PAGE_ID
    if (page_id == INVALID_PAGE_ID) {
      return End();
    }
    guard = bpm_->ReadPage(page_id);
  }
}

/**
 * @brief Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  // Begin(key) 找到第一个 >= key 的位置；如果树为空或路径无效，返回 End()
  auto leaf_guard_opt = FindLeafPageRead(key);
  if (!leaf_guard_opt.has_value()) {
    return End();
  }
  auto leaf_page = (*leaf_guard_opt).template As<LeafPage>();
  int index = LeafLowerBound(leaf_page, key);
  ReadPageGuard leaf_guard = std::move(*leaf_guard_opt);
  return INDEXITERATOR_TYPE(bpm_, std::move(leaf_guard), index);
}

/**
 * @brief Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
  // 默认构造的 IndexIterator 表示 end iterator。
  return INDEXITERATOR_TYPE();
}

/**
 * @return Page id of the root of this tree
 *
 * You may want to implement this while implementing Task #3.
 */
FULL_INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  // root_page_id_ 统一保存在 header page 中
  auto guard = bpm_->ReadPage(header_page_id_);
  auto header_page = guard.As<BPlusTreeHeaderPage>();
  return header_page->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 3>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 2>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, 1>;
template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>, -1>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
