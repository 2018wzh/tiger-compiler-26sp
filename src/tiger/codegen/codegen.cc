#include "tiger/codegen/codegen.h"

#include <cassert>
#include <sstream>

extern frame::RegManager *reg_manager;

namespace {

constexpr int maxlen = 1024;

} // namespace

namespace cg {

void CodeGen::Codegen() {
  auto *instr_list = new assem::InstrList();
  for (auto *stm : this->traces_->GetStmList()->GetList()) {
    stm->Munch(*instr_list, fs_);
  }
  this->assem_instr_ = std::make_unique<AssemInstr>(instr_list);
}

void AssemInstr::Print(FILE *out, temp::Map *map) const {
  for (auto instr : instr_list_->GetList())
    instr->Print(out, map);
  fprintf(out, "\n");
}

} // namespace cg

namespace tree {

/**
 * Generate code for passing arguments
 * @param args argument list
 * @param instr_holder instruction holder
 * @return temp list to hold arguments
 */
temp::TempList *ExpList::MunchArgs(assem::InstrList &instr_list,
                                   std::string_view fs) {
  auto *temps = new temp::TempList();
  auto it = exp_list_.begin();

  // ExternalCall inserts a magic staticLink sentinel that should not be
  // passed as a real argument.
  if (it != exp_list_.end()) {
    auto *name_exp = dynamic_cast<tree::NameExp *>(*it);
    if (name_exp != nullptr && name_exp->name_ != nullptr &&
        name_exp->name_->Name() == "staticLink")
      ++it;
  }

  for (; it != exp_list_.end(); ++it)
    temps->Append((*it)->Munch(instr_list, fs));

  return temps;
}

void SeqStm::Munch(assem::InstrList &, std::string_view) {
  // SeqStm should not exist after canonicalization.
  assert(0);
}

void LabelStm::Munch(assem::InstrList &instr_list, std::string_view) {
  instr_list.Append(new assem::LabelInstr(
      temp::LabelFactory::LabelString(this->label_), this->label_));
}

void JumpStm::Munch(assem::InstrList &instr_list, std::string_view) {
  instr_list.Append(new assem::OperInstr("jmp `j0", nullptr, nullptr,
                                         new assem::Targets(this->jumps_)));
}

void CjumpStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  std::string jmp;
  switch (this->op_) {
  case tree::RelOp::EQ_OP:
    jmp = "je";
    break;
  case tree::RelOp::NE_OP:
    jmp = "jne";
    break;
  case tree::RelOp::LT_OP:
    jmp = "jl";
    break;
  case tree::RelOp::GT_OP:
    jmp = "jg";
    break;
  case tree::RelOp::LE_OP:
    jmp = "jle";
    break;
  case tree::RelOp::GE_OP:
    jmp = "jge";
    break;
  case tree::RelOp::ULT_OP:
    jmp = "jb";
    break;
  case tree::RelOp::ULE_OP:
    jmp = "jbe";
    break;
  case tree::RelOp::UGT_OP:
    jmp = "ja";
    break;
  case tree::RelOp::UGE_OP:
    jmp = "jae";
    break;
  default:
    assert(0);
  }

  auto *left = this->left_->Munch(instr_list, fs);
  auto *right = this->right_->Munch(instr_list, fs);

  // cmpq src, dst  computes dst - src
  instr_list.Append(new assem::OperInstr(
      "cmpq `s1, `s0", nullptr, new temp::TempList({left, right}), nullptr));
  instr_list.Append(new assem::OperInstr(
      jmp + " `j0", nullptr, nullptr,
      new assem::Targets(new std::vector<temp::Label *>{this->true_label_})));
}

void MoveStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  if (auto *dst_mem = dynamic_cast<tree::MemExp *>(this->dst_)) {
    int offset = 0;
    bool matched = false;
    if (auto *binop = dynamic_cast<tree::BinopExp *>(dst_mem->exp_)) {
      auto *lhs_temp = dynamic_cast<tree::TempExp *>(binop->left_);
      auto *rhs_const = dynamic_cast<tree::ConstExp *>(binop->right_);
      if (lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = rhs_const->consti_;
        matched = true;
      }

      auto *lhs_const = dynamic_cast<tree::ConstExp *>(binop->left_);
      auto *rhs_temp = dynamic_cast<tree::TempExp *>(binop->right_);
      if (!matched && lhs_const != nullptr && rhs_temp != nullptr &&
          rhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = lhs_const->consti_;
        matched = true;
      }

      if (!matched && lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::MINUS_OP) {
        offset = -rhs_const->consti_;
        matched = true;
      }
    }

    if (matched) {
      auto *src = this->src_->Munch(instr_list, fs);
      if (src != nullptr && src == reg_manager->FramePointer()) {
        instr_list.Append(new assem::OperInstr(
            "movq %rbp, " + std::to_string(offset) + "(%rbp)", nullptr, nullptr,
            nullptr));
      } else {
        instr_list.Append(new assem::OperInstr(
            "movq `s0, " + std::to_string(offset) + "(%rbp)", nullptr,
            new temp::TempList(src), nullptr));
      }
      return;
    }

    auto *addr = dst_mem->exp_->Munch(instr_list, fs);
    auto *src = this->src_->Munch(instr_list, fs);
    instr_list.Append(new assem::OperInstr(
        "movq `s0, (`s1)", nullptr, new temp::TempList({src, addr}), nullptr));
    return;
  }

  auto *dst_temp = dynamic_cast<tree::TempExp *>(this->dst_);
  assert(dst_temp != nullptr);
  auto *src = this->src_->Munch(instr_list, fs);
  if (src != nullptr && src == reg_manager->FramePointer()) {
    instr_list.Append(new assem::OperInstr("movq %rbp, `d0",
                                           new temp::TempList(dst_temp->temp_),
                                           nullptr, nullptr));
    return;
  }
  instr_list.Append(new assem::MoveInstr("movq `s0, `d0",
                                         new temp::TempList(dst_temp->temp_),
                                         new temp::TempList(src)));
}

void ExpStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  this->exp_->Munch(instr_list, fs);
}

temp::Temp *BinopExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  if (this->op_ == tree::BinOp::PLUS_OP || this->op_ == tree::BinOp::MINUS_OP) {
    int offset = 0;
    bool matched = false;
    if (auto *binop = dynamic_cast<tree::BinopExp *>(this->left_)) {
      auto *lhs_temp = dynamic_cast<tree::TempExp *>(binop->left_);
      auto *rhs_const = dynamic_cast<tree::ConstExp *>(binop->right_);
      if (lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = rhs_const->consti_;
        matched = true;
      }

      auto *lhs_const = dynamic_cast<tree::ConstExp *>(binop->left_);
      auto *rhs_temp = dynamic_cast<tree::TempExp *>(binop->right_);
      if (!matched && lhs_const != nullptr && rhs_temp != nullptr &&
          rhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = lhs_const->consti_;
        matched = true;
      }

      if (!matched && lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::MINUS_OP) {
        offset = -rhs_const->consti_;
        matched = true;
      }
    }

    if (matched) {
      if (this->op_ == tree::BinOp::MINUS_OP)
        offset = -offset;
      auto *dst = temp::TempFactory::NewTemp();
      instr_list.Append(
          new assem::OperInstr("leaq " + std::to_string(offset) + "(%rbp), `d0",
                               new temp::TempList(dst), nullptr, nullptr));
      return dst;
    }

    matched = false;
    if (auto *binop = dynamic_cast<tree::BinopExp *>(this->right_)) {
      auto *lhs_temp = dynamic_cast<tree::TempExp *>(binop->left_);
      auto *rhs_const = dynamic_cast<tree::ConstExp *>(binop->right_);
      if (lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = rhs_const->consti_;
        matched = true;
      }

      auto *lhs_const = dynamic_cast<tree::ConstExp *>(binop->left_);
      auto *rhs_temp = dynamic_cast<tree::TempExp *>(binop->right_);
      if (!matched && lhs_const != nullptr && rhs_temp != nullptr &&
          rhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::PLUS_OP) {
        offset = lhs_const->consti_;
        matched = true;
      }

      if (!matched && lhs_temp != nullptr && rhs_const != nullptr &&
          lhs_temp->temp_ == reg_manager->FramePointer() &&
          binop->op_ == tree::BinOp::MINUS_OP) {
        offset = -rhs_const->consti_;
        matched = true;
      }
    }

    if (matched && this->op_ == tree::BinOp::PLUS_OP) {
      auto *dst = temp::TempFactory::NewTemp();
      instr_list.Append(
          new assem::OperInstr("leaq " + std::to_string(offset) + "(%rbp), `d0",
                               new temp::TempList(dst), nullptr, nullptr));
      return dst;
    }
  }

  auto *lhs = this->left_->Munch(instr_list, fs);

  switch (this->op_) {
  case tree::BinOp::PLUS_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr("addq `s0, `d0",
                                           new temp::TempList(dst),
                                           new temp::TempList({rhs, dst}),
                                           nullptr));
    return dst;
  }
  case tree::BinOp::MINUS_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr("subq `s0, `d0",
                                           new temp::TempList(dst),
                                           new temp::TempList({rhs, dst}),
                                           nullptr));
    return dst;
  }
  case tree::BinOp::MUL_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(reg_manager->ReturnValue()),
        new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr(
        "imulq `s0",
        new temp::TempList({reg_manager->ReturnValue(),
                            reg_manager->GetRegister(frame::X64RegManager::RDX)}),
        new temp::TempList(rhs), nullptr));
    instr_list.Append(
        new assem::MoveInstr("movq `s0, `d0", new temp::TempList(dst),
                             new temp::TempList(reg_manager->ReturnValue())));
    return dst;
  }
  case tree::BinOp::DIV_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *rax = reg_manager->ReturnValue();
    auto *rdx = reg_manager->GetRegister(frame::X64RegManager::RDX);
    if (lhs != rax) {
      instr_list.Append(new assem::MoveInstr(
          "movq `s0, `d0", new temp::TempList(rax), new temp::TempList(lhs)));
    }
    instr_list.Append(new assem::OperInstr(
        "cqto", new temp::TempList(rdx), new temp::TempList(rax), nullptr));
    instr_list.Append(new assem::OperInstr(
        "idivq `s0", new temp::TempList({rax, rdx}),
        new temp::TempList(rhs), nullptr));
    return rax;
  }
  case tree::BinOp::AND_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr("andq `s0, `d0",
                                           new temp::TempList(dst),
                                           new temp::TempList({rhs, dst}),
                                           nullptr));
    return dst;
  }
  case tree::BinOp::OR_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr("orq `s0, `d0",
                                           new temp::TempList(dst),
                                           new temp::TempList({rhs, dst}),
                                           nullptr));
    return dst;
  }
  case tree::BinOp::XOR_OP: {
    auto *rhs = this->right_->Munch(instr_list, fs);
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    instr_list.Append(new assem::OperInstr("xorq `s0, `d0",
                                           new temp::TempList(dst),
                                           new temp::TempList({rhs, dst}),
                                           nullptr));
    return dst;
  }
  case tree::BinOp::LSHIFT_OP: {
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    if (auto *c = dynamic_cast<tree::ConstExp *>(this->right_)) {
      instr_list.Append(
          new assem::OperInstr("salq $" + std::to_string(c->consti_) + ", `d0",
                               new temp::TempList(dst),
                               new temp::TempList(dst), nullptr));
    } else {
      auto *rhs = this->right_->Munch(instr_list, fs);
      instr_list.Append(new assem::OperInstr(
            "movq `s0, %rcx",
            new temp::TempList(reg_manager->GetRegister(frame::X64RegManager::RCX)),
            new temp::TempList(rhs), nullptr));
      instr_list.Append(new assem::OperInstr(
          "salq %cl, `d0", new temp::TempList(dst), new temp::TempList(dst),
          nullptr));
    }
    return dst;
  }
  case tree::BinOp::RSHIFT_OP: {
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    if (auto *c = dynamic_cast<tree::ConstExp *>(this->right_)) {
      instr_list.Append(
          new assem::OperInstr("shrq $" + std::to_string(c->consti_) + ", `d0",
                               new temp::TempList(dst),
                               new temp::TempList(dst), nullptr));
    } else {
      auto *rhs = this->right_->Munch(instr_list, fs);
      instr_list.Append(new assem::OperInstr(
            "movq `s0, %rcx",
            new temp::TempList(reg_manager->GetRegister(frame::X64RegManager::RCX)),
            new temp::TempList(rhs), nullptr));
      instr_list.Append(new assem::OperInstr(
          "shrq %cl, `d0", new temp::TempList(dst), new temp::TempList(dst),
          nullptr));
    }
    return dst;
  }
  case tree::BinOp::ARSHIFT_OP: {
    auto *dst = temp::TempFactory::NewTemp();
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList(dst), new temp::TempList(lhs)));
    if (auto *c = dynamic_cast<tree::ConstExp *>(this->right_)) {
      instr_list.Append(
          new assem::OperInstr("sarq $" + std::to_string(c->consti_) + ", `d0",
                               new temp::TempList(dst),
                               new temp::TempList(dst), nullptr));
    } else {
      auto *rhs = this->right_->Munch(instr_list, fs);
      instr_list.Append(new assem::OperInstr(
            "movq `s0, %rcx",
            new temp::TempList(reg_manager->GetRegister(frame::X64RegManager::RCX)),
            new temp::TempList(rhs), nullptr));
      instr_list.Append(new assem::OperInstr(
          "sarq %cl, `d0", new temp::TempList(dst), new temp::TempList(dst),
          nullptr));
    }
    return dst;
  }
  default:
    assert(0);
  }
  return nullptr;
}

temp::Temp *MemExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  int offset = 0;
  auto *dst = temp::TempFactory::NewTemp();
  bool matched = false;
  if (auto *binop = dynamic_cast<tree::BinopExp *>(this->exp_)) {
    auto *lhs_temp = dynamic_cast<tree::TempExp *>(binop->left_);
    auto *rhs_const = dynamic_cast<tree::ConstExp *>(binop->right_);
    if (lhs_temp != nullptr && rhs_const != nullptr &&
        lhs_temp->temp_ == reg_manager->FramePointer() &&
        binop->op_ == tree::BinOp::PLUS_OP) {
      offset = rhs_const->consti_;
      matched = true;
    }

    auto *lhs_const = dynamic_cast<tree::ConstExp *>(binop->left_);
    auto *rhs_temp = dynamic_cast<tree::TempExp *>(binop->right_);
    if (!matched && lhs_const != nullptr && rhs_temp != nullptr &&
        rhs_temp->temp_ == reg_manager->FramePointer() &&
        binop->op_ == tree::BinOp::PLUS_OP) {
      offset = lhs_const->consti_;
      matched = true;
    }

    if (!matched && lhs_temp != nullptr && rhs_const != nullptr &&
        lhs_temp->temp_ == reg_manager->FramePointer() &&
        binop->op_ == tree::BinOp::MINUS_OP) {
      offset = -rhs_const->consti_;
      matched = true;
    }
  }

  if (matched) {
    instr_list.Append(
        new assem::OperInstr("movq " + std::to_string(offset) + "(%rbp), `d0",
                             new temp::TempList(dst), nullptr, nullptr));
    return dst;
  }

  auto *addr = this->exp_->Munch(instr_list, fs);
  instr_list.Append(new assem::OperInstr("movq (`s0), `d0",
                                         new temp::TempList(dst),
                                         new temp::TempList(addr), nullptr));
  return dst;
}

temp::Temp *TempExp::Munch(assem::InstrList &, std::string_view) {
  return this->temp_;
}

temp::Temp *EseqExp::Munch(assem::InstrList &, std::string_view) {
  // EseqExp should not exist after canonicalization.
  assert(0);
  return nullptr;
}

temp::Temp *NameExp::Munch(assem::InstrList &instr_list, std::string_view) {
  auto *tmp = temp::TempFactory::NewTemp();
  instr_list.Append(new assem::OperInstr(
      "leaq " + temp::LabelFactory::LabelString(this->name_) + "(%rip), `d0",
      new temp::TempList(tmp), nullptr, nullptr));
  return tmp;
}

temp::Temp *ConstExp::Munch(assem::InstrList &instr_list, std::string_view) {
  auto *tmp = temp::TempFactory::NewTemp();
  instr_list.Append(
      new assem::OperInstr("movq $" + std::to_string(this->consti_) + ", `d0",
                           new temp::TempList(tmp), nullptr, nullptr));
  return tmp;
}

temp::Temp *CallExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto *args = this->args_->MunchArgs(instr_list, fs);
  auto *arg_regs = reg_manager->ArgRegs();
  const auto &arg_temps = args->GetList();
  const auto &regs = arg_regs->GetList();

  auto temp_it = arg_temps.begin();
  auto reg_it = regs.begin();
  auto *call_src = new temp::TempList();
  for (; temp_it != arg_temps.end() && reg_it != regs.end();
       ++temp_it, ++reg_it) {
    call_src->Append(*reg_it);
    if (*temp_it != nullptr && *temp_it == reg_manager->FramePointer()) {
      instr_list.Append(new assem::OperInstr(
          "movq %rbp, `d0", new temp::TempList(*reg_it), nullptr, nullptr));
    } else {
      instr_list.Append(new assem::MoveInstr("movq `s0, `d0",
                                             new temp::TempList(*reg_it),
                                             new temp::TempList(*temp_it)));
    }
  }

  auto stack_begin = temp_it;
  int stack_arg_count = std::distance(stack_begin, arg_temps.end());
  int stack_arg_bytes = stack_arg_count * reg_manager->WordSize();
  int stack_adjust = ((stack_arg_bytes + 15) / 16) * 16;
  if (stack_arg_count > 0) {
    instr_list.Append(new assem::OperInstr(
        "subq $" + std::to_string(stack_adjust) + ", %rsp",
        nullptr, nullptr, nullptr));
  }

  int stack_offset = 0;
  for (; temp_it != arg_temps.end();
       ++temp_it, stack_offset += reg_manager->WordSize()) {
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, " + std::to_string(stack_offset) + "(%rsp)", nullptr,
        new temp::TempList(*temp_it)));
  }

  std::string call_assem;
  if (auto *name_exp = dynamic_cast<tree::NameExp *>(this->fun_)) {
    call_assem = "call " + temp::LabelFactory::LabelString(name_exp->name_);
  } else {
    auto *fun_temp = this->fun_->Munch(instr_list, fs);
    call_assem = "call *`s0";
    call_src->Append(fun_temp);
  }

  instr_list.Append(new assem::OperInstr(
      call_assem, reg_manager->CallerSaves(), call_src, nullptr));

  if (stack_arg_count > 0) {
    instr_list.Append(new assem::OperInstr(
        "addq $" + std::to_string(stack_adjust) + ", %rsp",
        nullptr, nullptr, nullptr));
  }

  return reg_manager->ReturnValue();
}

} // namespace tree
