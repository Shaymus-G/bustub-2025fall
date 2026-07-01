//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seqscan_as_indexscan.cpp
//
// Identification: src/optimizer/seqscan_as_indexscan.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief Optimizes seq scan as index scan if there's an index on a table
 */

auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(P3): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::SeqScan) {
    return optimized_plan;
  }

  const auto &seq_scan_plan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);

  if (seq_scan_plan.filter_predicate_ == nullptr) {
    return optimized_plan;
  }

  struct IndexLookupInfo {
    index_oid_t index_oid_;
    std::vector<AbstractExpressionRef> pred_keys_;
  };

  // 解析单个等值点查条件，例如 v1 = 1 或 1 = v1
  auto extract_point_lookup = [this,
                               &seq_scan_plan](const AbstractExpressionRef &expr) -> std::optional<IndexLookupInfo> {
    const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());
    if (cmp_expr == nullptr || cmp_expr->comp_type_ != ComparisonType::Equal) {
      return std::nullopt;
    }

    const auto &left = cmp_expr->GetChildAt(0);
    const auto &right = cmp_expr->GetChildAt(1);

    const auto *left_col = dynamic_cast<const ColumnValueExpression *>(left.get());
    const auto *right_col = dynamic_cast<const ColumnValueExpression *>(right.get());

    const auto *left_const = dynamic_cast<const ConstantValueExpression *>(left.get());
    const auto *right_const = dynamic_cast<const ConstantValueExpression *>(right.get());

    const ColumnValueExpression *col_expr = nullptr;
    AbstractExpressionRef const_expr;

    if (left_col != nullptr && right_const != nullptr) {
      col_expr = left_col;
      const_expr = right;
    } else if (left_const != nullptr && right_col != nullptr) {
      col_expr = right_col;
      const_expr = left;
    } else {
      return std::nullopt;
    }

    if (col_expr->GetTupleIdx() != 0) {
      return std::nullopt;
    }

    auto index = MatchIndex(seq_scan_plan.table_name_, col_expr->GetColIdx());
    if (!index.has_value()) {
      return std::nullopt;
    }

    auto [index_oid, index_name] = index.value();
    return IndexLookupInfo{index_oid, std::vector<AbstractExpressionRef>{const_expr}};
  };

  // 递归解析 OR 连接的多个点查条件，例如 v1 = 1 OR v1 = 4
  std::function<std::optional<IndexLookupInfo>(const AbstractExpressionRef &)> extract_lookup_keys;
  extract_lookup_keys = [&](const AbstractExpressionRef &expr) -> std::optional<IndexLookupInfo> {
    const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get());

    if (logic_expr != nullptr && logic_expr->logic_type_ == LogicType::Or) {
      auto left = extract_lookup_keys(logic_expr->GetChildAt(0));
      auto right = extract_lookup_keys(logic_expr->GetChildAt(1));

      if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
      }

      if (left->index_oid_ != right->index_oid_) {
        return std::nullopt;
      }

      auto keys = left->pred_keys_;
      keys.insert(keys.end(), right->pred_keys_.begin(), right->pred_keys_.end());
      return IndexLookupInfo{left->index_oid_, std::move(keys)};
    }

    return extract_point_lookup(expr);
  };

  // 只有能完整匹配索引点查条件时，才将 SeqScan 替换为 IndexScan
  auto lookup = extract_lookup_keys(seq_scan_plan.filter_predicate_);
  if (!lookup.has_value()) {
    return optimized_plan;
  }

  return std::make_shared<IndexScanPlanNode>(seq_scan_plan.output_schema_, seq_scan_plan.table_oid_, lookup->index_oid_,
                                             seq_scan_plan.filter_predicate_, std::move(lookup->pred_keys_));
}

}  // namespace bustub
