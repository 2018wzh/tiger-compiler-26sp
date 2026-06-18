#include "tiger/liveness/liveness.h"

#include <algorithm>
#include <memory>

extern frame::RegManager *reg_manager;

namespace live {

bool MoveList::Contain(INodePtr src, INodePtr dst) {
  return std::any_of(move_list_.cbegin(), move_list_.cend(),
                     [src, dst](std::pair<INodePtr, INodePtr> move) {
                       return move.first == src && move.second == dst;
                     });
}

void MoveList::Delete(INodePtr src, INodePtr dst) {
  assert(src && dst);
  auto move_it = move_list_.begin();
  for (; move_it != move_list_.end(); move_it++) {
    if (move_it->first == src && move_it->second == dst) {
      break;
    }
  }
  move_list_.erase(move_it);
}

MoveList *MoveList::Union(MoveList *list) {
  auto *res = new MoveList();
  for (auto move : move_list_) {
    res->move_list_.push_back(move);
  }
  for (auto move : list->GetList()) {
    if (!res->Contain(move.first, move.second))
      res->move_list_.push_back(move);
  }
  return res;
}

MoveList *MoveList::Intersect(MoveList *list) {
  auto *res = new MoveList();
  for (auto move : list->GetList()) {
    if (Contain(move.first, move.second))
      res->move_list_.push_back(move);
  }
  return res;
}

void LiveGraphFactory::LiveMap() {
  // Helpers
  auto contains = [](const temp::TempList *list, temp::Temp *temp) {
    if (!list || !temp)
      return false;
    for (auto *item : list->GetList()) {
      if (item == temp)
        return true;
    }
    return false;
  };

  auto append_ = [&contains](temp::TempList *list, temp::Temp *temp) {
    if (!list || !temp || contains(list, temp))
      return false;
    list->Append(temp);
    return true;
  };

  auto union_ = [&append_](temp::TempList *dst, const temp::TempList *src) {
    bool flag = false;
    if (!dst || !src)
      return false;
    for (auto *temp : src->GetList())
      flag = append_(dst, temp) || flag;
    return flag;
  };

  auto copy = [&append_](const temp::TempList *src) {
    auto *res = new temp::TempList();
    if (!src)
      return res;
    for (auto *temp : src->GetList())
      append_(res, temp);
    return res;
  };

  auto diff = [&contains, &append_](const temp::TempList *lhs,
                                    const temp::TempList *rhs) {
    auto *res = new temp::TempList();
    if (!lhs)
      return res;
    for (auto *temp : lhs->GetList()) {
      if (!contains(rhs, temp))
        append_(res, temp);
    }
    return res;
  };

  // Init
  auto &nodes = flowgraph_->Nodes()->GetList();
  for (auto *node : nodes) {
    in_->Enter(node, new temp::TempList());
    out_->Enter(node, new temp::TempList());
  }
  // Build live graph
  bool flag = true;
  while (flag) {
    flag = false;
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
      auto *node = *it;
      auto *instr = node->NodeInfo();

      auto *use = instr->Use();
      auto *def = instr->Def();

      // If x is in use[n], it's in-live in node n
      auto *new_in = copy(use);

      // If x is out-live and not in def[n], it's in-live in node n
      auto *new_out = diff(out_->Look(node), def);
      union_(new_in, new_out);

      if (union_(in_->Look(node), new_in))
        flag = true;

      // If x is in-live at node n, then it's out-live in all pred[n]s.
      for (auto *pred : node->Pred()->GetList()) {
        if (union_(out_->Look(pred), in_->Look(node)))
          flag = true;
      }

      delete use;
      delete def;
      delete new_in;
      delete new_out;
    }
  }
}
void LiveGraphFactory::InterfGraph() {
  auto contains = [](const temp::TempList *list, temp::Temp *temp) {
    if (!list || !temp)
      return false;
    for (auto *item : list->GetList()) {
      if (item == temp)
        return true;
    }
    return false;
  };
  auto get_node = [](LiveGraphFactory *factory, temp::Temp *t) {
    assert(t);
    auto *node = factory->temp_node_map_->Look(t);
    if (node)
      return node;
    node = factory->live_graph_.interf_graph->NewNode(t);
    factory->temp_node_map_->Enter(t, node);
    return node;
  };
  for (auto *node : flowgraph_->Nodes()->GetList()) {
    auto *instr = node->NodeInfo();
    std::unique_ptr<temp::TempList> use(instr->Use());
    std::unique_ptr<temp::TempList> def(instr->Def());
    auto *in_temps = in_->Look(node);
    auto *out_temps = out_->Look(node);
    auto *move_instr = dynamic_cast<assem::MoveInstr *>(instr);

    if (use) {
      // Create nodes for each temp in use
      for (auto *temp : use->GetList())
        get_node(this, temp);
    }
    if (def) {
      // Create nodes for each temp in def
      for (auto *temp : def->GetList())
        get_node(this, temp);
    }
    // Create nodes for each temp in in_temps and out_temps
    for (auto *temp : in_temps->GetList())
      get_node(this, temp);
    for (auto *temp : out_temps->GetList())
      get_node(this, temp);

    if (move_instr && use && def && !use->GetList().empty() &&
        !def->GetList().empty()) {
      // Create nodes for the source and destination temps
      auto *src = use->GetList().front();
      auto *dst = def->GetList().front();
      live_graph_.moves->Append(get_node(this, src), get_node(this, dst));
    }

    // Create interferences
    if (def) {
      for (auto *dst_temp : def->GetList()) {
        auto *dst_node = get_node(this, dst_temp);
        for (auto *live_temp : out_temps->GetList()) {
          if (move_instr && contains(use.get(), live_temp))
            continue;
          if (dst_temp == live_temp)
            continue;
          auto *live_node = get_node(this, live_temp);
          this->live_graph_.interf_graph->AddEdge(dst_node, live_node);
        }
      }
    }

    // Non-move operations are treated as read-modify-write so the destination
    // cannot share a register with the other operands.
    if (!move_instr && def && use) {
      for (auto *dst_temp : def->GetList()) {
        auto *dst_node = get_node(this, dst_temp);
        for (auto *src_temp : use->GetList()) {
          if (src_temp == dst_temp)
            continue;
          auto *src_node = get_node(this, src_temp);
          this->live_graph_.interf_graph->AddEdge(dst_node, src_node);
        }
      }
    }
  }
}

void LiveGraphFactory::Liveness() {
  LiveMap();
  InterfGraph();
}

} // namespace live
