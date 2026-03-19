//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hyperloglog_presto.cpp
//
// Identification: src/primer/hyperloglog_presto.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/hyperloglog_presto.h"

namespace bustub {

/** @brief Parameterized constructor. */
template <typename KeyType>
HyperLogLogPresto<KeyType>::HyperLogLogPresto(int16_t n_leading_bits) 
: n_leading_bits_(n_leading_bits),
  m_(1ULL << n_leading_bits),
  registers_(m_, 0),
  cardinality_(0) {}

/** @brief Element is added for HLL calculation. */
template <typename KeyType>
auto HyperLogLogPresto<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */

  //1. 计算 64 位哈希值
  uint64_t hash = HashUtil::Hash64(val);
  //2. 提取高 n_leading_bits 位作为索引
  uint64_t index = 0;
  if(n_leading_bits_ > 0) {
    index = hash >> (64 - n_leading_bits_);
  }
  //3. 计算剩余位中第一个 '1' 的位置 (Rank)(将哈希值左移去掉索引位，然后计算前导零的个数)
  uint64_t remaining_bits = hash << n_leading_bits_;
  uint8_t rank = 1;
  if(remaining_bits == 0) {
    //如果剩余位全是0，Rank为剩余位数 + 1
    rank = 64 - n_leading_bits_ + 1;
  } else{
    //使用内置函数__builtin_clzll计算64位无符号整数的前导零个数
    rank = __builtin_clzll(remaining_bits) + 1;
  }
  //4. 更新桶中的最大Rank
  if(rank > registers_[index]) {
    registers_[index] = rank;
  }
}

/** @brief Function to compute cardinality. */
template <typename T>
auto HyperLogLogPresto<T>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */

  double sum = 0;
  //1. 计算所有桶的2^(-M[j])之和
  for(uint8_t rank : registers_) {
    sum += std::pow(2.0, -static_cast<double>(rank));
  }
  //2. 应用HLL核心公式
  double alpha = GetAlpha();
  double estimation = alpha * m_ * m_ / sum;
  //3. 更新cardinality_(Presto通常直接取floor后的整数)
  cardinality_ = static_cast<uint64_t>(std::floor(estimation));
}

template class HyperLogLogPresto<int64_t>;
template class HyperLogLogPresto<std::string>;
}  // namespace bustub
