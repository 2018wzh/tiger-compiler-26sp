#include "tiger/liveness/flowgraph.h"

#include <vector>

namespace fg {

void FlowGraphFactory::AssemFlowGraph() {
  this->flowgraph_ = new fg::FGraph();
  label_map_ = std::make_unique<tab::Table<temp::Label, FNode>>();

  std::vector<FNodePtr> nodes;
  nodes.reserve(this->instr_list_->GetList().size());

  for (auto instr : this->instr_list_->GetList()) {
    auto *node = this->flowgraph_->NewNode(instr);
    nodes.push_back(node);
    if (auto *label_instr = dynamic_cast<assem::LabelInstr *>(instr))
      this->label_map_->Enter(label_instr->label_, node);
  }

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    auto *node = nodes[i];
    auto *instr = node->NodeInfo();
    auto *oper_instr = dynamic_cast<assem::OperInstr *>(instr);

    bool fallthrough = true;
    if (oper_instr && oper_instr->jumps_) {
      fallthrough = oper_instr->assem_.rfind("jmp", 0) != 0;
      for (auto *label : *oper_instr->jumps_->labels_) {
        auto *jump_dest = this->label_map_->Look(label);
        if (jump_dest)
          this->flowgraph_->AddEdge(node, jump_dest);
      }
    }

    if (fallthrough && i + 1 < nodes.size())
      this->flowgraph_->AddEdge(node, nodes[i + 1]);
  }
}

} // namespace fg

namespace assem {

temp::TempList *LabelInstr::Def() const { return nullptr; }

temp::TempList *MoveInstr::Def() const {
  if (!this->dst_)
    return nullptr;
  return new temp::TempList{*this->dst_};
}

temp::TempList *OperInstr::Def() const {
  if (!this->dst_)
    return nullptr;
  return new temp::TempList{*this->dst_};
}

temp::TempList *LabelInstr::Use() const { return nullptr; }

temp::TempList *MoveInstr::Use() const {
  if (!this->src_)
    return nullptr;
  return new temp::TempList{*this->src_};
}

temp::TempList *OperInstr::Use() const {
  if (!this->src_)
    return nullptr;
  return new temp::TempList{*this->src_};
}
} // namespace assem
