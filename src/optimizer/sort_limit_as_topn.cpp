//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// sort_limit_as_topn.cpp
//
// Identification: src/optimizer/sort_limit_as_topn.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <utility>
#include <vector>

#include "execution/plans/limit_plan.h"
#include "execution/plans/sort_plan.h"
#include "execution/plans/topn_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 * @brief optimize sort + limit as top N
 */
auto Optimizer::OptimizeSortLimitAsTopN(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSortLimitAsTopN(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() != PlanType::Limit) {
    return optimized_plan;
  }

  const auto &limit_plan = dynamic_cast<const LimitPlanNode &>(*optimized_plan);

  if (limit_plan.GetLimit() >= 1000) {
    return optimized_plan;
  }

  auto sort_child = limit_plan.GetChildPlan();

  if (sort_child->GetType() != PlanType::Sort) {
    return optimized_plan;
  }

  const auto &sort_plan = dynamic_cast<const SortPlanNode &>(*sort_child);

  return std::make_shared<TopNPlanNode>(limit_plan.output_schema_, sort_plan.GetChildPlan(), sort_plan.GetOrderBy(),
                                        limit_plan.GetLimit());
}

}  // namespace bustub
