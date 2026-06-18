#include "tiger/regalloc/regalloc.h"

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

#include "tiger/output/logger.h"

extern frame::RegManager *reg_manager;

namespace ra {
Result::~Result() = default;

namespace {

using TempSet = std::unordered_set<temp::Temp *>;
using TempReplacements = std::unordered_map<temp::Temp *, temp::Temp *>;

std::vector<temp::Temp *> CollectSpilledTemps(temp::TempList *use,
                                              temp::TempList *def,
                                              const TempSet &spill_temps) {
  std::vector<temp::Temp *> temps;
  auto append = [&temps](temp::Temp *temp) {
    if (temp == nullptr)
      return;
    if (std::find(temps.begin(), temps.end(), temp) == temps.end())
      temps.push_back(temp);
  };
  if (use) {
    for (auto *temp : use->GetList()) {
      if (spill_temps.count(temp))
        append(temp);
    }
  }
  if (def) {
    for (auto *temp : def->GetList()) {
      if (spill_temps.count(temp))
        append(temp);
    }
  }
  return temps;
}

temp::TempList *RewriteTempList(temp::TempList *list,
                                const TempReplacements &replacements) {
  if (!list)
    return nullptr;

  auto *rewritten = new temp::TempList();
  for (auto *temp : list->GetList()) {
    auto it = replacements.find(temp);
    if (it != replacements.end())
      rewritten->Append(it->second);
    else
      rewritten->Append(temp);
  }
  return rewritten;
}

int AllocateSpillSlot(frame::Frame *frame, std::unordered_map<temp::Temp *, int> *slots,
                      temp::Temp *temp) {
  assert(frame);
  assert(slots);
  assert(temp);

  auto it = slots->find(temp);
  if (it != slots->end())
    return it->second;

  // Every new spill gets a dedicated stack slot in the current frame.
  frame->AllocLocal(true);
  int offset = -(static_cast<int>(frame->Formals()->size()) + frame->local_count_ + 1) *
               frame->word_size_;
  (*slots)[temp] = offset;
  return offset;
}

assem::Instr *CloneInstr(assem::Instr *instr, const TempReplacements &replacements) {
  if (auto *label = dynamic_cast<assem::LabelInstr *>(instr)) {
    return new assem::LabelInstr(label->assem_, label->label_);
  }

  if (auto *move = dynamic_cast<assem::MoveInstr *>(instr)) {
    return new assem::MoveInstr(move->assem_,
                                RewriteTempList(move->dst_, replacements),
                                RewriteTempList(move->src_, replacements));
  }

  if (auto *oper = dynamic_cast<assem::OperInstr *>(instr)) {
    return new assem::OperInstr(oper->assem_,
                                RewriteTempList(oper->dst_, replacements),
                                RewriteTempList(oper->src_, replacements),
                                oper->jumps_);
  }

  return nullptr;
}

assem::InstrList *RewriteProgram(
    assem::InstrList *body, frame::Frame *frame,
    std::unordered_map<temp::Temp *, int> *spill_slots,
    const TempSet &spill_temps) {
  auto *rewritten = new assem::InstrList();
  if (!body)
    return rewritten;

  for (auto *instr : body->GetList()) {
    std::unique_ptr<temp::TempList> use(instr->Use());
    std::unique_ptr<temp::TempList> def(instr->Def());
    TempReplacements replacements;
    std::vector<assem::Instr *> prefix;
    std::vector<assem::Instr *> suffix;

    auto spilled = CollectSpilledTemps(use.get(), def.get(), spill_temps);
    for (auto *temp : spilled) {
      auto *rewrite_temp = temp::TempFactory::NewTemp();
      replacements[temp] = rewrite_temp;

      int offset = AllocateSpillSlot(frame, spill_slots, temp);
      bool need_load = use && std::find(use->GetList().begin(), use->GetList().end(), temp) != use->GetList().end();
      bool need_store = def && std::find(def->GetList().begin(), def->GetList().end(), temp) != def->GetList().end();

      if (need_load) {
        prefix.push_back(new assem::OperInstr(
            "movq " + std::to_string(offset) + "(%rbp), `d0",
            new temp::TempList(rewrite_temp), nullptr, nullptr));
      }
      if (need_store) {
        suffix.push_back(new assem::OperInstr(
            "movq `s0, " + std::to_string(offset) + "(%rbp)", nullptr,
            new temp::TempList(rewrite_temp), nullptr));
      }
    }

    for (auto *pre : prefix)
      rewritten->Append(pre);

    auto *cloned = CloneInstr(instr, replacements);
    if (cloned != nullptr)
      rewritten->Append(cloned);

    for (auto *post : suffix)
      rewritten->Append(post);
  }

  return rewritten;
}

} // namespace

void RegAllocator::RegAlloc() {
  // Rebuild the analysis after each spill rewrite until coloring succeeds.
  auto *current_il = assem_instr_->GetInstrList();
  std::unordered_map<temp::Temp *, int> spill_slots;

  while (true) {
    fg::FlowGraphFactory flow_graph_factory(current_il);
    flow_graph_factory.AssemFlowGraph();

    live::LiveGraphFactory live_graph_factory(flow_graph_factory.GetFlowGraph());
    live_graph_factory.Liveness();

    col::Color colorer(live_graph_factory.GetLiveGraph());
    col::Result color_result = colorer.Coloring();

    if (color_result.spills == nullptr ||
        color_result.spills->GetList().empty()) {
      coloring_ = color_result.coloring;
      il_ = current_il;
      result_ = std::make_unique<Result>(coloring_, il_);
      return;
    }

    TempSet spill_temps;
    for (auto *node : color_result.spills->GetList()) {
      if (node && node->NodeInfo())
        spill_temps.insert(node->NodeInfo());
    }

    current_il = RewriteProgram(current_il, frame_, &spill_slots, spill_temps);
    assem_instr_ = std::make_unique<cg::AssemInstr>(current_il);
  }
}

std::unique_ptr<Result> RegAllocator::BuildAllocationResult() {
  return std::move(result_);
}
} // namespace ra
