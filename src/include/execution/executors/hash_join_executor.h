//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"

namespace bustub {
/** HashJoin 的连接键。 */
struct HashJoinKey {
  std::vector<Value> keys_;

  auto operator==(const HashJoinKey &other) const -> bool {
    if (keys_.size() != other.keys_.size()) {
      return false;
    }

    for (size_t i = 0; i < keys_.size(); i++) {
      if (keys_[i].CompareEquals(other.keys_[i]) != CmpBool::CmpTrue) {
        return false;
      }
    }

    return true;
  }
};
}  // namespace bustub

namespace std {
/** 为 HashJoinKey 实现 std::hash。 */
template <>
struct hash<bustub::HashJoinKey> {
  auto operator()(const bustub::HashJoinKey &join_key) const -> std::size_t {
    size_t curr_hash = 0;
    for (const auto &key : join_key.keys_) {
      if (!key.IsNull()) {
        curr_hash = bustub::HashUtil::CombineHashes(curr_hash, bustub::HashUtil::HashValue(&key));
      }
    }
    return curr_hash;
  }
};
}  // namespace std

namespace bustub {

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  void Init() override;

  auto Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch, size_t batch_size)
      -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  auto MakeLeftJoinKey(const Tuple *tuple) -> HashJoinKey {
    std::vector<Value> keys;
    keys.reserve(plan_->LeftJoinKeyExpressions().size());

    for (const auto &expr : plan_->LeftJoinKeyExpressions()) {
      keys.emplace_back(expr->Evaluate(tuple, left_child_->GetOutputSchema()));
    }

    return {keys};
  }

  auto MakeRightJoinKey(const Tuple *tuple) -> HashJoinKey {
    std::vector<Value> keys;
    keys.reserve(plan_->RightJoinKeyExpressions().size());

    for (const auto &expr : plan_->RightJoinKeyExpressions()) {
      keys.emplace_back(expr->Evaluate(tuple, right_child_->GetOutputSchema()));
    }

    return {keys};
  }

  auto HasNullKey(const HashJoinKey &key) -> bool {
    return std::any_of(key.keys_.begin(), key.keys_.end(), [](const auto &value) { return value.IsNull(); });
  }

  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;

  /** 左侧子执行器。 */
  std::unique_ptr<AbstractExecutor> left_child_;

  /** 右侧子执行器。 */
  std::unique_ptr<AbstractExecutor> right_child_;

  /** 右表构建出的哈希表。 */
  std::unordered_map<HashJoinKey, std::vector<Tuple>> hash_table_;

  /** 物化后的连接结果。 */
  std::vector<Tuple> result_tuples_;

  /** 当前输出到 result_tuples_ 的位置。 */
  size_t cursor_{0};
};

}  // namespace bustub
