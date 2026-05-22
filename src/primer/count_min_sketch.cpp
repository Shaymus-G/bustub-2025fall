//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// count_min_sketch.cpp
//
// Identification: src/primer/count_min_sketch.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/count_min_sketch.h"

#include <stdexcept>
#include <string>

#include <algorithm>
#include <limits>
#include <shared_mutex>
#include <vector>

namespace bustub {

/**
 * Constructor for the count-min sketch.
 *
 * @param width The width of the sketch matrix.
 * @param depth The depth of the sketch matrix.
 * @throws std::invalid_argument if width or depth are zero.
 */
template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(uint32_t width, uint32_t depth) : width_(width), depth_(depth) {
  /** @TODO(student) Implement this function! */

  if (width == 0 || depth == 0) {
    throw std::invalid_argument("Width and Depth must be greater than zero.");
  }
  // 初始化二维表格，全部填充为1
  table_.assign(depth_, std::vector<uint32_t>(width_, 0));
  // 初始化锁阵列：分配width*depth个锁
  locks_ = std::make_unique<std::shared_mutex[]>(width * depth);

  /** @fall2025 PLEASE DO NOT MODIFY THE FOLLOWING */
  // Initialize seeded hash functions
  hash_functions_.reserve(depth_);
  for (size_t i = 0; i < depth_; i++) {
    hash_functions_.push_back(this->HashFunction(i));
  }
}

template <typename KeyType>
CountMinSketch<KeyType>::CountMinSketch(CountMinSketch &&other) noexcept : width_(other.width_), depth_(other.depth_) {
  /** @TODO(student) Implement this function! */

  hash_functions_ = std::move(other.hash_functions_);
  table_ = std::move(other.table_);
  // 移动locks的所有权
  locks_ = std::move(other.locks_);
}

template <typename KeyType>
auto CountMinSketch<KeyType>::operator=(CountMinSketch &&other) noexcept -> CountMinSketch & {
  /** @TODO(student) Implement this function! */

  if (this != &other) {
    // 此处locks_会在赋值时自动释放旧资源并接管新资源
    width_ = other.width_;
    depth_ = other.depth_;
    hash_functions_ = std::move(other.hash_functions_);
    table_ = std::move(other.table_);
    // 移动 locks_ 的所有权，旧的 locks_ 会被析构。
    locks_ = std::move(other.locks_);
  }
  return *this;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Insert(const KeyType &item) {
  /** @TODO(student) Implement this function! */

  // 确定所有要获取的锁的索引
  std::vector<size_t> lock_indices;
  lock_indices.reserve(depth_);
  for (size_t i = 0; i < depth_; ++i) {
    size_t col = hash_functions_[i](item);
    lock_indices.push_back(i * width_ + col);
  }
  // 对锁索引进行排序，以固定顺序获取锁，防止死锁
  std::sort(lock_indices.begin(), lock_indices.end());
  // 如果某个item的哈希结果导致不同行的col相同，排序后会有重复索引，std::unique 可以在排序后移除重复项
  lock_indices.erase(std::unique(lock_indices.begin(), lock_indices.end()), lock_indices.end());
  // 获取所有需要的写锁，由于lint限制不能使用 <mutex>，这里按固定顺序手动 lock 和 unlock
  for (size_t index : lock_indices) {
    locks_[index].lock();
  }

  for (size_t i = 0; i < depth_; ++i) {
    size_t col = hash_functions_[i](item);
    table_[i][col]++;
  }

  for (auto iter = lock_indices.rbegin(); iter != lock_indices.rend(); ++iter) {
    locks_[*iter].unlock();
  }
  // 确保所有锁都获取后，再进行实际的计数更新
}

template <typename KeyType>
void CountMinSketch<KeyType>::Merge(const CountMinSketch<KeyType> &other) {
  if (width_ != other.width_ || depth_ != other.depth_) {
    throw std::invalid_argument("Incompatible CountMinSketch dimensions for merge.");
  }
  /** @TODO(student) Implement this function! */

  // 按固定顺序获取所有写锁，避免并发 Merge / Clear / Insert 死锁
  for (size_t i = 0; i < depth_ * width_; ++i) {
    locks_[i].lock();
  }

  for (size_t i = 0; i < depth_; ++i) {
    for (size_t j = 0; j < width_; ++j) {
      table_[i][j] += other.table_[i][j];
    }
  }

  for (size_t i = depth_ * width_; i > 0; --i) {
    locks_[i - 1].unlock();
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::Count(const KeyType &item) const -> uint32_t {
  uint32_t min_count = std::numeric_limits<uint32_t>::max();
  // 估算值是所有哈希映射位置中的最小值
  for (size_t i = 0; i < depth_; ++i) {
    size_t col = hash_functions_[i](item);
    // 只获取当前单元格的读锁
    std::shared_lock<std::shared_mutex> cell_lock(locks_[i * width_ + col]);
    min_count = std::min(min_count, table_[i][col]);
  }
  return min_count;
}

template <typename KeyType>
void CountMinSketch<KeyType>::Clear() {
  /** @TODO(student) Implement this function! */

  // 按固定顺序获取所有写锁，避免并发 Clear / Insert / Merge 死锁
  for (size_t i = 0; i < depth_ * width_; ++i) {
    locks_[i].lock();
  }

  for (auto &row : table_) {
    std::fill(row.begin(), row.end(), 0);
  }

  for (size_t i = depth_ * width_; i > 0; --i) {
    locks_[i - 1].unlock();
  }
}

template <typename KeyType>
auto CountMinSketch<KeyType>::TopK(uint16_t k, const std::vector<KeyType> &candidates)
    -> std::vector<std::pair<KeyType, uint32_t>> {
  /** @TODO(student) Implement this function! */

  // Count 已经加锁，为了简化和避免死锁风险，此处无需加锁
  std::vector<std::pair<KeyType, uint32_t>> counts;
  counts.reserve(candidates.size());
  // 1. 获取所有候选人的估算值
  for (const auto &item : candidates) {
    counts.push_back({item, Count(item)});
  }
  // 2. 按频率从高到低排序，如果频率相同可以按 Key 排序
  std::sort(counts.begin(), counts.end(), [](const auto &a, const auto &b) {
    return a.second > b.second;  // 降序
  });
  // 3. 取前 k 个
  uint16_t actual_k = std::min(k, static_cast<uint16_t>(counts.size()));
  counts.resize(actual_k);
  return counts;
}

// Explicit instantiations for all types used in tests
template class CountMinSketch<std::string>;
template class CountMinSketch<int64_t>;  // For int64_t tests
template class CountMinSketch<int>;      // This covers both int and int32_t
}  // namespace bustub
