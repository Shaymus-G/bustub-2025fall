//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nlj_as_hash_join.cpp
//
// Identification: src/optimizer/nlj_as_hash_join.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

/**
 * @brief optimize nested loop join into hash join.
 * In the starter code, we will check NLJs with exactly one equal condition. You can further support optimizing joins
 * with multiple eq conditions.
 */
auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for Spring 2025: You should support join keys of any number of conjunction of equi-conditions:
  // E.g. <column expr> = <column expr> AND <column expr> = <column expr> AND ...
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::NestedLoopJoin) {
    return optimized_plan;
  }

  const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);

  std::vector<AbstractExpressionRef> left_keys;
  std::vector<AbstractExpressionRef> right_keys;

  auto extract_equal_join_key = [&](const AbstractExpressionRef &expr) -> bool {
    const auto *cmp_expr = dynamic_cast<const ComparisonExpression *>(expr.get());
    if (cmp_expr == nullptr || cmp_expr->comp_type_ != ComparisonType::Equal) {
      return false;
    }

    const auto &left_expr = cmp_expr->GetChildAt(0);
    const auto &right_expr = cmp_expr->GetChildAt(1);

    const auto *left_col = dynamic_cast<const ColumnValueExpression *>(left_expr.get());
    const auto *right_col = dynamic_cast<const ColumnValueExpression *>(right_expr.get());

    if (left_col == nullptr || right_col == nullptr) {
      return false;
    }

    if (left_col->GetTupleIdx() == 0 && right_col->GetTupleIdx() == 1) {
      left_keys.emplace_back(left_expr);
      right_keys.emplace_back(right_expr);
      return true;
    }

    if (left_col->GetTupleIdx() == 1 && right_col->GetTupleIdx() == 0) {
      left_keys.emplace_back(right_expr);
      right_keys.emplace_back(left_expr);
      return true;
    }

    return false;
  };

  std::function<bool(const AbstractExpressionRef &)> extract_all_join_keys;
  extract_all_join_keys = [&](const AbstractExpressionRef &expr) -> bool {
    const auto *logic_expr = dynamic_cast<const LogicExpression *>(expr.get());

    if (logic_expr != nullptr && logic_expr->logic_type_ == LogicType::And) {
      return extract_all_join_keys(logic_expr->GetChildAt(0)) && extract_all_join_keys(logic_expr->GetChildAt(1));
    }

    return extract_equal_join_key(expr);
  };

  if (nlj_plan.Predicate() == nullptr || !extract_all_join_keys(nlj_plan.Predicate()) || left_keys.empty()) {
    return optimized_plan;
  }

  return std::make_shared<HashJoinPlanNode>(nlj_plan.output_schema_, nlj_plan.GetLeftPlan(), nlj_plan.GetRightPlan(),
                                            std::move(left_keys), std::move(right_keys), nlj_plan.GetJoinType());
}

}  // namespace bustub
