//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// window_function_executor.cpp
//
// Identification: src/execution/window_function_executor.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/window_function_executor.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <numeric>
#include <tuple>
#include <vector>
#include "execution/execution_common.h"
#include "execution/plans/window_plan.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

namespace {
auto ValuesEqualWithNull(const std::vector<Value> &left, const std::vector<Value> &right) -> bool {
  if (left.size() != right.size()) {
    return false;
  }

  for (size_t i = 0; i < left.size(); i++) {
    if (left[i].IsNull() && right[i].IsNull()) {
      continue;
    }

    if (left[i].CompareEquals(right[i]) != CmpBool::CmpTrue) {
      return false;
    }
  }

  return true;
}

auto CompareSortKeys(const SortKey &key_a, const SortKey &key_b, const std::vector<OrderBy> &order_bys) -> bool {
  for (size_t i = 0; i < order_bys.size(); i++) {
    const auto order_type = std::get<0>(order_bys[i]);
    const auto null_order = std::get<1>(order_bys[i]);

    const auto &value_a = key_a[i];
    const auto &value_b = key_b[i];

    const bool a_is_null = value_a.IsNull();
    const bool b_is_null = value_b.IsNull();

    if (a_is_null || b_is_null) {
      if (a_is_null && b_is_null) {
        continue;
      }

      bool nulls_first = true;
      if (null_order == OrderByNullType::NULLS_FIRST) {
        nulls_first = true;
      } else if (null_order == OrderByNullType::NULLS_LAST) {
        nulls_first = false;
      } else {
        nulls_first = order_type != OrderByType::DESC;
      }

      return a_is_null ? nulls_first : !nulls_first;
    }

    if (value_a.CompareEquals(value_b) == CmpBool::CmpTrue) {
      continue;
    }

    const bool ascending = order_type != OrderByType::DESC;
    const bool less_than = value_a.CompareLessThan(value_b) == CmpBool::CmpTrue;
    return ascending ? less_than : !less_than;
  }

  return false;
}

auto EvaluatePartitionKey(const Tuple &tuple, const Schema &schema,
                          const std::vector<AbstractExpressionRef> &partition_by) -> std::vector<Value> {
  std::vector<Value> key;
  key.reserve(partition_by.size());

  for (const auto &expr : partition_by) {
    key.emplace_back(expr->Evaluate(&tuple, schema));
  }

  return key;
}

auto CombineWindowValue(const WindowFunctionPlanNode::WindowFunction &window_function, Value *result,
                        const Value &input_value, bool *has_value, int32_t *count) {
  switch (window_function.type_) {
    case WindowFunctionType::CountStarAggregate:
      (*count)++;
      *result = ValueFactory::GetIntegerValue(*count);
      break;

    case WindowFunctionType::CountAggregate:
      if (!input_value.IsNull()) {
        (*count)++;
      }
      *result = ValueFactory::GetIntegerValue(*count);
      break;

    case WindowFunctionType::SumAggregate:
      if (!input_value.IsNull()) {
        *result = *has_value ? result->Add(input_value) : input_value;
        *has_value = true;
      }
      break;

    case WindowFunctionType::MinAggregate:
      if (!input_value.IsNull()) {
        *result = *has_value ? result->Min(input_value) : input_value;
        *has_value = true;
      }
      break;

    case WindowFunctionType::MaxAggregate:
      if (!input_value.IsNull()) {
        *result = *has_value ? result->Max(input_value) : input_value;
        *has_value = true;
      }
      break;

    case WindowFunctionType::Rank:
      break;
  }
}

auto ComputeWindowValueForRow(const WindowFunctionPlanNode::WindowFunction &window_function,
                              const std::vector<Tuple> &input_tuples, const Schema &input_schema, size_t row_idx)
    -> Value {
  std::vector<size_t> partition_rows;
  const auto current_partition_key =
      EvaluatePartitionKey(input_tuples[row_idx], input_schema, window_function.partition_by_);

  for (size_t i = 0; i < input_tuples.size(); i++) {
    const auto partition_key = EvaluatePartitionKey(input_tuples[i], input_schema, window_function.partition_by_);
    if (ValuesEqualWithNull(current_partition_key, partition_key)) {
      partition_rows.push_back(i);
    }
  }

  std::vector<SortKey> sort_keys(input_tuples.size());
  if (!window_function.order_by_.empty()) {
    for (const auto idx : partition_rows) {
      sort_keys[idx] = GenerateSortKey(input_tuples[idx], window_function.order_by_, input_schema);
    }

    std::stable_sort(partition_rows.begin(), partition_rows.end(), [&](const auto left_idx, const auto right_idx) {
      return CompareSortKeys(sort_keys[left_idx], sort_keys[right_idx], window_function.order_by_);
    });
  }

  if (window_function.type_ == WindowFunctionType::Rank) {
    if (window_function.order_by_.empty()) {
      return ValueFactory::GetIntegerValue(1);
    }

    size_t first_peer_pos = 0;
    const auto &current_sort_key = sort_keys[row_idx];

    for (size_t pos = 0; pos < partition_rows.size(); pos++) {
      const auto idx = partition_rows[pos];
      if (ValuesEqualWithNull(sort_keys[idx], current_sort_key)) {
        first_peer_pos = pos;
        break;
      }
    }

    return ValueFactory::GetIntegerValue(static_cast<int32_t>(first_peer_pos + 1));
  }

  std::vector<size_t> frame_rows;

  if (window_function.order_by_.empty()) {
    frame_rows = partition_rows;
  } else {
    size_t current_pos = 0;
    for (size_t pos = 0; pos < partition_rows.size(); pos++) {
      if (partition_rows[pos] == row_idx) {
        current_pos = pos;
        break;
      }
    }

    for (size_t pos = 0; pos <= current_pos; pos++) {
      frame_rows.push_back(partition_rows[pos]);
    }
  }

  Value result = ValueFactory::GetNullValueByType(TypeId::INTEGER);
  bool has_value = false;
  int32_t count = 0;

  for (const auto idx : frame_rows) {
    Value input_value = ValueFactory::GetNullValueByType(TypeId::INTEGER);

    if (window_function.type_ != WindowFunctionType::CountStarAggregate) {
      input_value = window_function.function_->Evaluate(&input_tuples[idx], input_schema);
    }

    CombineWindowValue(window_function, &result, input_value, &has_value, &count);
  }

  if (window_function.type_ == WindowFunctionType::CountAggregate ||
      window_function.type_ == WindowFunctionType::CountStarAggregate) {
    return ValueFactory::GetIntegerValue(count);
  }

  return result;
}

auto ComparePartitionKeys(const std::vector<Value> &key_a, const std::vector<Value> &key_b) -> int {
  for (size_t i = 0; i < key_a.size(); i++) {
    const bool a_is_null = key_a[i].IsNull();
    const bool b_is_null = key_b[i].IsNull();

    if (a_is_null || b_is_null) {
      if (a_is_null && b_is_null) {
        continue;
      }
      return a_is_null ? -1 : 1;
    }

    if (key_a[i].CompareEquals(key_b[i]) == CmpBool::CmpTrue) {
      continue;
    }

    if (key_a[i].CompareLessThan(key_b[i]) == CmpBool::CmpTrue) {
      return -1;
    }

    return 1;
  }

  return 0;
}

auto BuildWindowOutputOrder(const std::vector<Tuple> &input_tuples, const Schema &input_schema,
                            const WindowFunctionPlanNode *plan) -> std::vector<size_t> {
  std::vector<size_t> output_order(input_tuples.size());
  std::iota(output_order.begin(), output_order.end(), 0);

  if (input_tuples.empty() || plan->window_functions_.empty()) {
    return output_order;
  }

  const auto canonical_window_iter =
      std::min_element(plan->window_functions_.begin(), plan->window_functions_.end(),
                       [](const auto &left, const auto &right) { return left.first < right.first; });

  const auto &window_function = canonical_window_iter->second;

  if (window_function.partition_by_.empty() && window_function.order_by_.empty()) {
    return output_order;
  }

  std::stable_sort(output_order.begin(), output_order.end(), [&](const auto left_idx, const auto right_idx) {
    const auto left_partition_key =
        EvaluatePartitionKey(input_tuples[left_idx], input_schema, window_function.partition_by_);
    const auto right_partition_key =
        EvaluatePartitionKey(input_tuples[right_idx], input_schema, window_function.partition_by_);

    const auto partition_cmp = ComparePartitionKeys(left_partition_key, right_partition_key);
    if (partition_cmp != 0) {
      return partition_cmp < 0;
    }

    if (!window_function.order_by_.empty()) {
      const auto left_sort_key = GenerateSortKey(input_tuples[left_idx], window_function.order_by_, input_schema);
      const auto right_sort_key = GenerateSortKey(input_tuples[right_idx], window_function.order_by_, input_schema);

      if (CompareSortKeys(left_sort_key, right_sort_key, window_function.order_by_)) {
        return true;
      }

      if (CompareSortKeys(right_sort_key, left_sort_key, window_function.order_by_)) {
        return false;
      }
    }

    return left_idx < right_idx;
  });

  return output_order;
}
}  // namespace

/**
 * Construct a new WindowFunctionExecutor instance.
 * @param exec_ctx The executor context
 * @param plan The window aggregation plan to be executed
 */
WindowFunctionExecutor::WindowFunctionExecutor(ExecutorContext *exec_ctx, const WindowFunctionPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

/** Initialize the window aggregation */
void WindowFunctionExecutor::Init() {
  child_executor_->Init();

  result_tuples_.clear();
  cursor_ = 0;

  std::vector<Tuple> input_tuples;
  std::vector<Tuple> child_tuples;
  std::vector<RID> child_rids;

  while (child_executor_->Next(&child_tuples, &child_rids, BUSTUB_BATCH_SIZE)) {
    input_tuples.insert(input_tuples.end(), child_tuples.begin(), child_tuples.end());
  }

  const auto &input_schema = child_executor_->GetOutputSchema();

  const auto output_order = BuildWindowOutputOrder(input_tuples, input_schema, plan_);

  for (const auto row_idx : output_order) {
    const auto &input_tuple = input_tuples[row_idx];
    std::vector<Value> values;
    values.reserve(plan_->columns_.size());

    for (uint32_t col_idx = 0; col_idx < plan_->columns_.size(); col_idx++) {
      auto window_iter = plan_->window_functions_.find(col_idx);

      if (window_iter == plan_->window_functions_.end()) {
        values.emplace_back(plan_->columns_[col_idx]->Evaluate(&input_tuple, input_schema));
        continue;
      }

      values.emplace_back(ComputeWindowValueForRow(window_iter->second, input_tuples, input_schema, row_idx));
    }

    result_tuples_.emplace_back(values, &GetOutputSchema());
  }
}

/**
 * Yield the next tuple batch from the window aggregation.
 * @param[out] tuple_batch The next tuple batch produced by the window aggregation
 * @param[out] rid_batch The next tuple RID batch produced by the window aggregation
 * @param batch_size The number of tuples to be included in the batch (default: BUSTUB_BATCH_SIZE)
 * @return `true` if a tuple was produced, `false` if there are no more tuples
 */
auto WindowFunctionExecutor::Next(std::vector<bustub::Tuple> *tuple_batch, std::vector<bustub::RID> *rid_batch,
                                  size_t batch_size) -> bool {
  tuple_batch->clear();
  rid_batch->clear();

  while (cursor_ < result_tuples_.size() && tuple_batch->size() < batch_size) {
    tuple_batch->push_back(result_tuples_[cursor_]);
    rid_batch->emplace_back();
    cursor_++;
  }

  return !tuple_batch->empty();
}
}  // namespace bustub
