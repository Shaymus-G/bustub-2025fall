//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hyperloglog.cpp
//
// Identification: src/primer/hyperloglog.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "primer/hyperloglog.h"

namespace bustub {

/** @brief Parameterized constructor. */
template <typename KeyType>
HyperLogLog<KeyType>::HyperLogLog(int16_t n_bits) : cardinality_(0),n_bits_(n_bits) {
  //桶的数量m=2^n_bits
  size_t m=1ULL<<n_bits;
  registers_.assign(m,0);
}

/**
 * @brief Function that computes binary.
 *
 * @param[in] hash
 * @returns binary of a given hash
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  /** @TODO(student) Implement this function! */

  //直接利用bitset的构造函数将uint64_t转为二进制位
  return std::bitset<BITSET_CAPACITY>(hash);
}

/**
 * @brief Function that computes leading zeros.
 *
 * @param[in] bset - binary values of a given bitset
 * @returns leading zeros of given binary set
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::PositionOfLeftmostOne(const std::bitset<BITSET_CAPACITY> &bset) const -> uint64_t {
  /** @TODO(student) Implement this function! */

  //从索引位后开始向下查找，假设hash是64位，n_bits位用于索引，剩下的位从（63-n_bits)到0
  int start_bit=BITSET_CAPACITY-1-n_bits_;
  for(int i=start_bit;i>=0;--i){
    if(bset.test(i)){
      //位置为距离开始查找的点的偏移量+1
      return static_cast<uint64_t>(start_bit-i+1);
    }
  }
  //如果全是0，返回剩下的位数+1，即起始点加2
  return static_cast<uint64_t>(start_bit+2);
}

/**
 * @brief Adds a value into the HyperLogLog.
 *
 * @param[in] val - value that's added into hyperloglog
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::AddElem(KeyType val) -> void {
  /** @TODO(student) Implement this function! */

  //计算哈希并转为bitset
  hash_t hash=CalculateHash(val);
  std::bitset<BITSET_CAPACITY> bset=ComputeBinary(hash);
  //提取高n_bits位作为索引
  uint64_t index=0;
  for(int i=0;i<n_bits_;++i){
    if(bset.test(BITSET_CAPACITY-1-i)){
      index |=(1ULL<<(n_bits_-1-i));
    }
  }
  //计算左起第一个1的位置
  uint64_t pos=PositionOfLeftmostOne(bset);
  //更新对应桶的最大值
  if(pos>registers_[index]){
    registers_[index]=static_cast<uint8_t>(pos);
  }
}

/**
 * @brief Function that computes cardinality.
 */
template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeCardinality() -> void {
  /** @TODO(student) Implement this function! */

  double m=static_cast<double>(registers_.size());
  double sum=0.0;
  for(uint8_t val:registers_){
    sum+=std::pow(2.0,-static_cast<double>(val));
  }
  //公式：E=alpha*m^2/sum
  double estimation=CONSTANT*m*m/sum;
  cardinality_=static_cast<size_t>(std::floor(estimation));
}

template class HyperLogLog<int64_t>;
template class HyperLogLog<std::string>;

}  // namespace bustub
