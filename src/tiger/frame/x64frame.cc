#include "tiger/frame/x64frame.h"

extern frame::RegManager *reg_manager;

namespace frame {
namespace {

int AlignTo(int value, int alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

} // namespace

X64RegManager::X64RegManager() : RegManager() {
  for (int i = 0; i < REG_COUNT; i++)
    regs_.push_back(temp::TempFactory::NewTemp());

  // Note: no frame pointer in tiger compiler
  std::array<std::string_view, REG_COUNT> reg_name{
      "%rax", "%rbx", "%rcx", "%rdx", "%rsi", "%rdi", "%rbp", "%rsp",
      "%r8",  "%r9",  "%r10", "%r11", "%r12", "%r13", "%r14", "%r15"};
  int reg = RAX;
  for (auto &name : reg_name) {
    temp_map_->Enter(regs_[reg], new std::string(name));
    reg++;
  }

  // Frame pointer is represented explicitly in IR and should print as %rbp.
  temp_map_->Enter(regs_[FP], new std::string("%rbp"));
}

temp::TempList *X64RegManager::Registers() {
  const std::array reg_array{
      RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8, R9, R10, R11, R12, R13, R14, R15,
  };
  auto *temp_list = new temp::TempList();
  for (auto &reg : reg_array)
    temp_list->Append(regs_[reg]);
  return temp_list;
}

temp::TempList *X64RegManager::ArgRegs() {
  const std::array reg_array{RDI, RSI, RDX, RCX, R8, R9};
  auto *temp_list = new temp::TempList();
  ;
  for (auto &reg : reg_array)
    temp_list->Append(regs_[reg]);
  return temp_list;
}

temp::TempList *X64RegManager::CallerSaves() {
  std::array reg_array{RAX, RDI, RSI, RDX, RCX, R8, R9, R10, R11};
  auto *temp_list = new temp::TempList();
  ;
  for (auto &reg : reg_array)
    temp_list->Append(regs_[reg]);
  return temp_list;
}

temp::TempList *X64RegManager::CalleeSaves() {
  std::array reg_array{RBP, RBX, R12, R13, R14, R15};
  auto *temp_list = new temp::TempList();
  ;
  for (auto &reg : reg_array)
    temp_list->Append(regs_[reg]);
  return temp_list;
}

temp::TempList *X64RegManager::ReturnSink() {
  temp::TempList *temp_list = CalleeSaves();
  temp_list->Append(regs_[SP]);
  temp_list->Append(regs_[RV]);
  return temp_list;
}

int X64RegManager::WordSize() { return 8; }

temp::Temp *X64RegManager::FramePointer() { return regs_[FP]; }

temp::Temp *X64RegManager::StackPointer() { return regs_[SP]; }

temp::Temp *X64RegManager::ReturnValue() { return regs_[RV]; }

class InFrameAccess : public Access {
public:
  int offset;

  explicit InFrameAccess(int offset) : offset(offset) {}
  tree::Exp *ToExp(tree::Exp *frame_ptr) const override {
    return new tree::MemExp(new tree::BinopExp(tree::PLUS_OP, frame_ptr,
                                               new tree::ConstExp(offset)));
  }
};

class InRegAccess : public Access {
public:
  temp::Temp *reg;

  explicit InRegAccess(temp::Temp *reg) : reg(reg) {}
  tree::Exp *ToExp(tree::Exp *framePtr) const override {
    return new tree::TempExp(reg);
  }
};

class X64Frame : public Frame {
public:
  tree::Stm *view_shift;

  X64Frame(temp::Label *name, std::list<frame::Access *> *formals)
      : Frame(8, 0, name, formals), view_shift(nullptr) {}

  [[nodiscard]] std::string GetLabel() const override { return name_->Name(); }
  [[nodiscard]] temp::Label *Name() const override { return name_; }
  [[nodiscard]] std::list<frame::Access *> *Formals() const override {
    return formals_;
  }
  frame::Access *AllocLocal(bool escape) override {
    if (escape) {
      local_count_++;
      int offset =
          -(static_cast<int>(formals_->size()) + local_count_ + 1) * word_size_;
      return new InFrameAccess(offset);
    } else {
      return new InRegAccess(temp::TempFactory::NewTemp());
    }
  }
  void AllocOutgoSpace(int size) override {
    // Outgo space is used to store arguments when calling other functions. We
    // allocate outgo space on the caller frame, so callee frame does not need
    // to allocate outgo space.
    return;
  }
  void SetViewShift(tree::Stm *stm) override { view_shift = stm; }
  [[nodiscard]] std::string GetFrameLabel() const override {
    return name_->Name();
  }
};

frame::Frame *NewFrame(temp::Label *name, std::list<bool> formals) {
  std::list<frame::Access *> *access_list = new std::list<frame::Access *>();

  if (!formals.empty()) {
    bool static_link_escape = formals.back();
    formals.pop_back();
    if (static_link_escape) {
      access_list->push_back(new InFrameAccess(-access_list->size() * 8 - 16));
    } else {
      access_list->push_back(new InRegAccess(temp::TempFactory::NewTemp()));
    }
  }

  for (bool escape : formals) {
    if (escape) {
      access_list->push_back(new InFrameAccess(-access_list->size() * 8 - 16));
    } else {
      access_list->push_back(new InRegAccess(temp::TempFactory::NewTemp()));
    }
  }
  return new X64Frame(name, access_list);
}

tree::Exp *ExternalCall(std::string_view s, tree::ExpList *args) {
  // Prepend a magic exp at first arg, indicating do not pass static link on
  // stack
  args->Insert(new tree::NameExp(temp::LabelFactory::NamedLabel("staticLink")));
  return new tree::CallExp(new tree::NameExp(temp::LabelFactory::NamedLabel(s)),
                           args);
}

/**
 * Moving incoming formal parameters, the saving and restoring of callee-save
 * Registers
 * @param frame curruent frame
 * @param stm statements
 * @return statements with saving, restoring and view shift
 */
tree::Stm *ProcEntryExit1(frame::Frame *frame, tree::Stm *stm) {
  auto x64_frame = dynamic_cast<frame::X64Frame *>(frame);
  assert(x64_frame);

  auto callee_list = new tree::ExpList();

  // Save callee-saved register
  tree::Stm *save_stm = nullptr;
  temp::TempList *callees = reg_manager->CalleeSaves();
  for (auto callee : callees->GetList()) {
    temp::Temp *r = temp::TempFactory::NewTemp();
    if (!save_stm)
      save_stm =
          new tree::MoveStm(new tree::TempExp(r), new tree::TempExp(callee));
    else
      save_stm = new tree::SeqStm(
          save_stm,
          new tree::MoveStm(new tree::TempExp(r), new tree::TempExp(callee)));
    callee_list->Append(new tree::TempExp(r));
  }

  // Restore callee-saved register
  tree::Stm *restore_stm = nullptr;
  callees = reg_manager->CalleeSaves();
  auto callee_it = callee_list->GetList().begin();
  for (auto callee : callees->GetList()) {
    assert(callee_it != callee_list->GetList().end());
    if (!restore_stm)
      restore_stm = new tree::MoveStm(new tree::TempExp(callee), *callee_it++);
    else
      restore_stm = new tree::SeqStm(
          restore_stm,
          new tree::MoveStm(new tree::TempExp(callee), *callee_it++));
  }

  // Add view shift for arguments
  tree::Stm *exit_stm;
  if (x64_frame->view_shift == nullptr) {
    // Outermost frame and functions with no formals do not have formal access_
    // list and view shift
    exit_stm = new tree::SeqStm(save_stm, new tree::SeqStm(stm, restore_stm));
  } else
    exit_stm = new tree::SeqStm(
        save_stm, new tree::SeqStm(x64_frame->view_shift,
                                   new tree::SeqStm(stm, restore_stm)));
  return exit_stm;
}

assem::Proc *ProcEntryExit3(frame::Frame *frame, assem::InstrList *body) {
  auto x64_frame = dynamic_cast<frame::X64Frame *>(frame);
  assert(x64_frame);

  std::string prolog = x64_frame->GetLabel() + ":\n";
  prolog += "subq $8, %rsp\n";
  prolog += "movq %rbp, (%rsp)\n";
  prolog += "movq %rsp, %rbp\n";
  int formal_area = (static_cast<int>(x64_frame->Formals()->size()) + 1) *
                    x64_frame->word_size_;
  int local_area = x64_frame->local_count_ == 0
                       ? 0
                       : (static_cast<int>(x64_frame->Formals()->size()) +
                          x64_frame->local_count_ + 1) *
                             x64_frame->word_size_;
  int frame_size = std::max(formal_area, local_area);
  // Keep the stack aligned before calls made from this procedure.
  frame_size = AlignTo(frame_size, 16);
  if (frame_size > 0)
    prolog += "subq $" + std::to_string(frame_size) + ", %rsp\n";

  // The interpreter and runtime expect a normal function epilogue.
  std::string epilog;
  epilog += "movq %rbp, %rsp\n";
  epilog += "movq (%rsp), %rbp\n";
  epilog += "addq $8, %rsp\n";
  epilog += "retq\n";

  return new assem::Proc(prolog, body, epilog);
}

assem::Proc *BuildCompleteProcedure(frame::Frame *frame,
                                    assem::InstrList *body) {
  // The x64 backend already knows how to wrap the final body with prologue
  // and epilogue code, so this helper just forwards to that routine.
  return ProcEntryExit3(frame, body);
}

} // namespace frame
