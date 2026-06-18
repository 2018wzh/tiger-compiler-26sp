#include "tiger/regalloc/color.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern frame::RegManager *reg_manager;

namespace col {
namespace {

using NodeSet = std::unordered_set<live::INodePtr>;
using ColorMap = std::unordered_map<live::INodePtr, std::string>;

int TempOrder(live::INodePtr node) {
  if (!node || !node->NodeInfo())
    return std::numeric_limits<int>::min();
  return node->NodeInfo()->Int();
}

std::string *LookupRegName(temp::Temp *temp) {
  if (!temp)
    return nullptr;
  return reg_manager->temp_map_->Look(temp);
}

void AppendUnique(std::vector<live::INodePtr> *nodes, live::INodePtr node) {
  if (!nodes || !node)
    return;
  if (std::find(nodes->begin(), nodes->end(), node) == nodes->end())
    nodes->push_back(node);
}

std::unordered_map<live::INodePtr, std::vector<live::INodePtr>>
BuildAdjacency(live::IGraphPtr interf_graph) {
  std::unordered_map<live::INodePtr, std::vector<live::INodePtr>> adj;
  if (!interf_graph)
    return adj;

  for (auto *node : interf_graph->Nodes()->GetList()) {
    auto &neighbors = adj[node];
    for (auto *succ : node->Succ()->GetList())
      AppendUnique(&neighbors, succ);
    for (auto *pred : node->Pred()->GetList())
      AppendUnique(&neighbors, pred);
    std::sort(neighbors.begin(), neighbors.end(),
              [](live::INodePtr lhs, live::INodePtr rhs) {
                if (TempOrder(lhs) != TempOrder(rhs))
                  return TempOrder(lhs) < TempOrder(rhs);
                return lhs->Key() < rhs->Key();
              });
  }
  return adj;
}

bool IsPrecolored(live::INodePtr node, const std::vector<temp::Temp *> &regs) {
  if (!node || !node->NodeInfo())
    return false;
  auto *temp = node->NodeInfo();
  if (temp == reg_manager->FramePointer() ||
      temp == reg_manager->StackPointer())
    return true;
  return std::find(regs.begin(), regs.end(), temp) != regs.end();
}

std::string GetFixedColor(temp::Temp *temp) {
  auto *name = LookupRegName(temp);
  if (!name)
    return {};
  return *name;
}

int DegreeInActiveSet(
    live::INodePtr node, const NodeSet &active,
    const std::unordered_map<live::INodePtr, std::vector<live::INodePtr>>
        &adj) {
  int degree = 0;
  auto it = adj.find(node);
  if (it == adj.end())
    return degree;
  for (auto *neighbor : it->second) {
    if (active.find(neighbor) != active.end())
      degree++;
  }
  return degree;
}

} // namespace

Color::Color(live::LiveGraph live_graph) : live_graph_(live_graph) {
  std::unique_ptr<temp::TempList> regs(reg_manager->Registers());
  for (auto *temp : regs->GetList()) {
    registers_.push_back(temp);
    auto *name = LookupRegName(temp);
    if (name != nullptr && *name != "%rbp" && *name != "%rax")
      available_.push_back(temp);
  }
}

Result Color::Coloring() {
  // Step 1: collect graph nodes and build a stable neighbor cache.
  auto *interf_graph = live_graph_.interf_graph;
  auto adjacency = BuildAdjacency(interf_graph);
  std::vector<live::INodePtr> nodes;
  if (interf_graph) {
    for (auto *node : interf_graph->Nodes()->GetList())
      nodes.push_back(node);
  }
  std::sort(nodes.begin(), nodes.end(),
            [](live::INodePtr lhs, live::INodePtr rhs) {
              if (TempOrder(lhs) != TempOrder(rhs))
                return TempOrder(lhs) < TempOrder(rhs);
              return lhs->Key() < rhs->Key();
            });

  // Step 2: precolor machine registers so they behave like fixed colors.
  ColorMap fixed_colors;
  for (auto *node : nodes) {
    if (!IsPrecolored(node, registers_))
      continue;
    auto *temp = node->NodeInfo();
    auto color = GetFixedColor(temp);
    if (!color.empty())
      fixed_colors.emplace(node, std::move(color));
  }

  // Step 3: simplify the graph and pick spill candidates when needed.
  NodeSet active;
  for (auto *node : nodes) {
    if (fixed_colors.find(node) == fixed_colors.end())
      active.insert(node);
  }

  std::vector<live::INodePtr> select_stack;
  const int k = static_cast<int>(available_.size());
  while (!active.empty()) {
    live::INodePtr simplify = nullptr;
    for (auto *node : nodes) {
      if (active.find(node) == active.end())
        continue;
      if (DegreeInActiveSet(node, active, adjacency) < k) {
        simplify = node;
        break;
      }
    }

    if (simplify != nullptr) {
      select_stack.push_back(simplify);
      active.erase(simplify);
      continue;
    }

    live::INodePtr spill = nullptr;
    int best_degree = -1;
    int best_temp = std::numeric_limits<int>::max();
    for (auto *node : nodes) {
      if (active.find(node) == active.end())
        continue;
      int degree = DegreeInActiveSet(node, active, adjacency);
      int temp_order = TempOrder(node);
      if (degree > best_degree ||
          (degree == best_degree && temp_order < best_temp)) {
        spill = node;
        best_degree = degree;
        best_temp = temp_order;
      }
    }

    if (spill == nullptr)
      break;

    select_stack.push_back(spill);
    active.erase(spill);
  }

  // Step 4: color in reverse order and mark nodes that cannot be colored.
  ColorMap assigned = fixed_colors;
  auto *spills = new live::INodeList();

  for (auto it = select_stack.rbegin(); it != select_stack.rend(); ++it) {
    auto *node = *it;
    if (fixed_colors.find(node) != fixed_colors.end())
      continue;

    std::set<std::string> blocked;
    auto neighbor_it = adjacency.find(node);
    if (neighbor_it != adjacency.end()) {
      for (auto *neighbor : neighbor_it->second) {
        auto assigned_it = assigned.find(neighbor);
        if (assigned_it != assigned.end())
          blocked.insert(assigned_it->second);
      }
    }

    std::string chosen;
    for (auto *reg : available_) {
      auto *reg_name = LookupRegName(reg);
      if (!reg_name)
        continue;
      if (blocked.find(*reg_name) == blocked.end()) {
        chosen = *reg_name;
        break;
      }
    }

    if (chosen.empty()) {
      spills->Append(node);
      continue;
    }

    assigned[node] = std::move(chosen);
  }

  // Step 5: export the successful assignments in the format used by regalloc.
  auto *coloring = temp::Map::Empty();
  for (const auto &entry : assigned) {
    auto *node = entry.first;
    if (!node || !node->NodeInfo())
      continue;
    if (fixed_colors.find(node) != fixed_colors.end())
      continue;
    coloring->Enter(node->NodeInfo(), new std::string(entry.second));
  }

  if (spills->GetList().empty()) {
    delete spills;
    spills = nullptr;
  }
  return Result(coloring, spills);
}
} // namespace col
