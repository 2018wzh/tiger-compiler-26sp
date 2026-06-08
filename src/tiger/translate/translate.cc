#include "tiger/translate/translate.h"

#include <tiger/absyn/absyn.h>

#include "tiger/env/env.h"
#include "tiger/errormsg/errormsg.h"
#include "tiger/frame/frame.h"
#include "tiger/frame/temp.h"
#include "tiger/frame/x64frame.h"

#include <iterator>

extern frame::Frags *frags;
extern frame::RegManager *reg_manager;

namespace {
frame::ProcFrag *ProcEntryExit(tr::Level *level, tr::Exp *body);
}

namespace tr {

Access *Access::AllocLocal(Level *level, bool escape) {
  if (escape) {
    return new Access(level, level->frame_->AllocLocal(true));
  } else {
    return new Access(level, level->frame_->AllocLocal(false));
  }
}

class Cx {
public:
  PatchList trues_;
  PatchList falses_;
  tree::Stm *stm_;

  Cx(PatchList trues, PatchList falses, tree::Stm *stm)
      : trues_(trues), falses_(falses), stm_(stm) {}
};

class Exp {
public:
  [[nodiscard]] virtual tree::Exp *UnEx() const = 0;
  [[nodiscard]] virtual tree::Stm *UnNx() const = 0;
  [[nodiscard]] virtual Cx UnCx(err::ErrorMsg *errormsg) const = 0;
};

class ExpAndTy {
public:
  tr::Exp *exp_;
  type::Ty *ty_;

  ExpAndTy(tr::Exp *exp, type::Ty *ty) : exp_(exp), ty_(ty) {}
};

class ExExp : public Exp {
public:
  tree::Exp *exp_;

  explicit ExExp(tree::Exp *exp) : exp_(exp) {}

  [[nodiscard]] tree::Exp *UnEx() const override {
    // Keep the expression unchanged
    return this->exp_;
  }
  [[nodiscard]] tree::Stm *UnNx() const override {
    // Evaluate the expression for side effects and discard the result.
    // eg: exp; -> EXP(exp);
    return new tree::ExpStm(this->exp_);
  }
  [[nodiscard]] Cx UnCx(err::ErrorMsg *errormsg) const override {
    // Convert the expression to a conditional jump statement.
    // eg: if (exp) goto true_label else goto false_label; -> CJUMP(exp != 0,
    // true_label, false_label);
    temp::Label *true_label = temp::LabelFactory::NewLabel();
    temp::Label *false_label = temp::LabelFactory::NewLabel();
    auto *stm =
        new tree::CjumpStm(tree::NE_OP, this->exp_, new tree::ConstExp(0),
                           true_label, false_label);
    return Cx(PatchList({&stm->true_label_}), PatchList({&stm->false_label_}),
              stm);
  }
};

class NxExp : public Exp {
public:
  tree::Stm *stm_;

  explicit NxExp(tree::Stm *stm) : stm_(stm) {}

  [[nodiscard]] tree::Exp *UnEx() const override {
    // Convert the statement to an expression by evaluating the statement and
    // returning 0 as the result
    // eg: stm; -> ESEQ(stm, CONST 0);
    return new tree::EseqExp(this->stm_, new tree::ConstExp(0));
  }
  [[nodiscard]] tree::Stm *UnNx() const override {
    // Keep the statement unchanged
    return this->stm_;
  }
  [[nodiscard]] Cx UnCx(err::ErrorMsg *errormsg) const override {
    // Statements do not naturally yield a boolean value. Preserve the side
    // effect and then use an always-false conditional jump.
    temp::Label *true_label = temp::LabelFactory::NewLabel();
    temp::Label *false_label = temp::LabelFactory::NewLabel();
    auto *cjump =
        new tree::CjumpStm(tree::NE_OP, new tree::ConstExp(0),
                           new tree::ConstExp(0), true_label, false_label);
    tree::Stm *stm = new tree::SeqStm(this->stm_, cjump);
    return Cx(PatchList({&cjump->true_label_}),
              PatchList({&cjump->false_label_}), stm);
  }
};

class CxExp : public Exp {
public:
  Cx cx_;

  CxExp(PatchList trues, PatchList falses, tree::Stm *stm)
      : cx_(trues, falses, stm) {}

  [[nodiscard]] tree::Exp *UnEx() const override {
    // Materialize the boolean result in a temporary.
    /*
    eg: if (stm) goto true_label else goto false_label; ->
    ESEQ(
      SEQ(stm,
        SEQ(LABEL true_label,
          SEQ(MOVE(temp, 1),
            SEQ(JUMP(join_label),
              SEQ(LABEL false_label,
                SEQ(MOVE(temp, 0),
                  LABEL join_label)))))),
      TEMP temp)
    */
    temp::Temp *temp = temp::TempFactory::NewTemp();
    temp::Label *true_label = temp::LabelFactory::NewLabel();
    temp::Label *false_label = temp::LabelFactory::NewLabel();
    temp::Label *join_label = temp::LabelFactory::NewLabel();

    Cx cx = this->cx_;
    cx.trues_.DoPatch(true_label);
    cx.falses_.DoPatch(false_label);

    tree::Stm *stm = new tree::SeqStm(
        cx.stm_, new tree::SeqStm(
                     new tree::LabelStm(true_label),
                     new tree::SeqStm(
                         new tree::MoveStm(new tree::TempExp(temp),
                                           new tree::ConstExp(1)),
                         new tree::SeqStm(
                             new tree::JumpStm(
                                 new tree::NameExp(join_label),
                                 new std::vector<temp::Label *>{join_label}),
                             new tree::SeqStm(
                                 new tree::LabelStm(false_label),
                                 new tree::SeqStm(
                                     new tree::MoveStm(new tree::TempExp(temp),
                                                       new tree::ConstExp(0)),
                                     new tree::LabelStm(join_label)))))));
    return new tree::EseqExp(stm, new tree::TempExp(temp));
  }
  [[nodiscard]] tree::Stm *UnNx() const override {
    // Materialize the boolean value and discard it.
    return new tree::ExpStm(this->UnEx());
  }
  [[nodiscard]] Cx UnCx(err::ErrorMsg *errormsg) const override {
    // Keep the conditional jump statement unchanged
    return this->cx_;
  }
};

void ProgTr::Translate() {
  this->FillBaseTEnv();
  this->FillBaseVEnv();
  tr::ExpAndTy *root_exp_ty = this->absyn_tree_->Translate(
      this->venv_.get(), this->tenv_.get(), this->main_level_.get(), nullptr,
      this->errormsg_.get());
  if (root_exp_ty) {
    frags->PushBack(ProcEntryExit(this->main_level_.get(), root_exp_ty->exp_));
  }
}

} // namespace tr

namespace {

/**
 * Wrapper for `ProcEntryExit1`, which deals with the return value of the
 * function body
 * @param level current level
 * @param body function body
 * @return procedure fragment after `ProcEntryExit1`
 */
frame::ProcFrag *ProcEntryExit(tr::Level *level, tr::Exp *body) {
  tree::Stm *body_stm = new tree::MoveStm(
      new tree::TempExp(reg_manager->ReturnValue()), body->UnEx());
  return new frame::ProcFrag(frame::ProcEntryExit1(level->frame_, body_stm),
                             level->frame_);
}
} // namespace
namespace absyn {

tr::ExpAndTy *AbsynTree::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   tr::Level *level, temp::Label *label,
                                   err::ErrorMsg *errormsg) const {
  if (root_) {
    return root_->Translate(venv, tenv, level, label, errormsg);
  } else {
    errormsg->Error(0, "no expression");
    return new tr::ExpAndTy(nullptr, type::VoidTy::Instance());
  }
}

tr::ExpAndTy *SimpleVar::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   tr::Level *level, temp::Label *label,
                                   err::ErrorMsg *errormsg) const {
  // eg: var x -> access(var x) from the correct frame via static links
  env::EnvEntry *entry = venv->Look(sym_);
  if (!entry) {
    errormsg->Error(pos_, "undefined variable %s", sym_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  if (typeid(*entry) != typeid(env::VarEntry)) {
    errormsg->Error(pos_, "%s is not a variable", sym_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto var_entry = static_cast<env::VarEntry *>(entry);
  if (!var_entry->access_) {
    errormsg->Error(pos_, "variable %s is not accessible", sym_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto access = var_entry->access_;
  tree::Exp *frame_ptr = new tree::TempExp(reg_manager->FramePointer());
  while (level && level != access->level_) {
    auto *formals = level->Formals();
    assert(formals && !formals->empty());
    frame_ptr = formals->front()->access_->ToExp(frame_ptr);
    level = level->parent_;
  }
  assert(level == access->level_);
  tree::Exp *exp = access->access_->ToExp(frame_ptr);
  return new tr::ExpAndTy(new tr::ExExp(exp), var_entry->ty_->ActualTy());
}

tr::ExpAndTy *FieldVar::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                  tr::Level *level, temp::Label *label,
                                  err::ErrorMsg *errormsg) const {
  // eg: var x.f -> MEM(BINOP(PLUS, var x, CONST offset))
  tr::ExpAndTy *exp_ty = var_->Translate(venv, tenv, level, label, errormsg);
  tr::Exp *exp = exp_ty->exp_;
  type::Ty *ty = exp_ty->ty_->ActualTy();

  if (typeid(*ty) != typeid(type::RecordTy)) {
    errormsg->Error(pos_, "not a record type");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }

  if (typeid(*exp) != typeid(tr::ExExp)) {
    errormsg->Error(pos_, "field var's exp must be an expression");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto record_ty = static_cast<type::RecordTy *>(ty);
  type::FieldList *field_list = record_ty->fields_;
  int order = 0;
  for (auto field : field_list->GetList()) {
    if (field->name_ == sym_) {
      tree::Exp *texp = new tree::MemExp(new tree::BinopExp(
          tree::PLUS_OP, exp->UnEx(),
          new tree::ConstExp(order * reg_manager->WordSize())));
      return new tr::ExpAndTy(new tr::ExExp(texp), field->ty_->ActualTy());
    }
    order++;
  }
  errormsg->Error(pos_, "field %s doesn't exist", sym_->Name().data());
  return new tr::ExpAndTy(nullptr, type::IntTy::Instance());
}

tr::ExpAndTy *SubscriptVar::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                      tr::Level *level, temp::Label *label,
                                      err::ErrorMsg *errormsg) const {
  // eg: var x[exp] -> MEM(BINOP(PLUS, var x, BINOP(MUL, exp, CONST word_size)))
  tr::ExpAndTy *exp_ty = var_->Translate(venv, tenv, level, label, errormsg);
  tr::Exp *exp = exp_ty->exp_;
  type::Ty *ty = exp_ty->ty_->ActualTy();
  if (typeid(*ty) != typeid(type::ArrayTy)) {
    errormsg->Error(pos_, "not an array type");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto array_ty = static_cast<type::ArrayTy *>(ty);
  tr::ExpAndTy *subscript_exp_ty =
      subscript_->Translate(venv, tenv, level, label, errormsg);
  tr::Exp *subscript_exp = subscript_exp_ty->exp_;
  type::Ty *subscript_ty = subscript_exp_ty->ty_->ActualTy();
  if (typeid(*subscript_ty) != typeid(type::IntTy)) {
    errormsg->Error(pos_, "array subscript must be an integer");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  tree::Exp *texp = new tree::MemExp(new tree::BinopExp(
      tree::PLUS_OP, exp->UnEx(),
      new tree::BinopExp(tree::MUL_OP, subscript_exp->UnEx(),
                         new tree::ConstExp(reg_manager->WordSize()))));
  return new tr::ExpAndTy(new tr::ExExp(texp), array_ty->ty_->ActualTy());
}

tr::ExpAndTy *VarExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: exp var -> var
  return var_->Translate(venv, tenv, level, label, errormsg);
}

tr::ExpAndTy *NilExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: nil -> CONST 0
  return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                          type::NilTy::Instance());
}

tr::ExpAndTy *IntExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: int -> CONST int
  return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(val_)),
                          type::IntTy::Instance());
}

tr::ExpAndTy *StringExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   tr::Level *level, temp::Label *label,
                                   err::ErrorMsg *errormsg) const {
  // eg: string -> NAME str_label
  temp::Label *str_label = temp::LabelFactory::NewLabel();
  frags->PushBack(new frame::StringFrag(str_label, str_));
  return new tr::ExpAndTy(new tr::ExExp(new tree::NameExp(str_label)),
                          type::StringTy::Instance());
}

tr::ExpAndTy *CallExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                 tr::Level *level, temp::Label *label,
                                 err::ErrorMsg *errormsg) const {
  // eg: func(args) -> CALL(NAME func_label, static_link, args) or an external
  // call
  env::EnvEntry *entry = venv->Look(func_);
  if (!entry) {
    errormsg->Error(pos_, "undefined function %s", func_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  if (typeid(*entry) != typeid(env::FunEntry)) {
    errormsg->Error(pos_, "%s is not a function", func_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto fun_entry = static_cast<env::FunEntry *>(entry);
  auto *args = new tree::ExpList();
  for (auto arg : this->args_->GetList()) {
    tr::ExpAndTy *arg_exp_ty =
        arg->Translate(venv, tenv, level, label, errormsg);
    args->Append(arg_exp_ty->exp_->UnEx());
  }
  tree::Exp *call_exp = nullptr;
  if (!fun_entry->label_) {
    call_exp = frame::ExternalCall(func_->Name(), args);
  } else {
    tree::Exp *static_link = new tree::TempExp(reg_manager->FramePointer());
    tr::Level *curr_level = level;
    while (curr_level && curr_level != fun_entry->level_->parent_) {
      auto *formals = curr_level->Formals();
      assert(formals && !formals->empty());
      static_link = formals->front()->access_->ToExp(static_link);
      curr_level = curr_level->parent_;
    }
    assert(curr_level == fun_entry->level_->parent_);
    args->Insert(static_link);
    call_exp = new tree::CallExp(new tree::NameExp(fun_entry->label_), args);
  }
  if (fun_entry->result_->ActualTy() == type::VoidTy::Instance()) {
    return new tr::ExpAndTy(new tr::NxExp(new tree::ExpStm(call_exp)),
                            fun_entry->result_->ActualTy());
  } else {
    return new tr::ExpAndTy(new tr::ExExp(call_exp),
                            fun_entry->result_->ActualTy());
  }
}

tr::ExpAndTy *OpExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                               tr::Level *level, temp::Label *label,
                               err::ErrorMsg *errormsg) const {
  // eg: left op right -> BINOP(op, left, right)
  tr::ExpAndTy *left_exp_ty =
      left_->Translate(venv, tenv, level, label, errormsg);
  tr::ExpAndTy *right_exp_ty =
      right_->Translate(venv, tenv, level, label, errormsg);
  type::Ty *left_ty = left_exp_ty->ty_->ActualTy();
  type::Ty *right_ty = right_exp_ty->ty_->ActualTy();
  tree::Exp *left_exp = left_exp_ty->exp_->UnEx();
  tree::Exp *right_exp = right_exp_ty->exp_->UnEx();
  switch (this->oper_) {
  // Bin ops
  case PLUS_OP:
  case MINUS_OP:
  case TIMES_OP:
  case DIVIDE_OP:
  case AND_OP:
  case OR_OP: {
    if (typeid(*left_ty) != typeid(type::IntTy) ||
        typeid(*right_ty) != typeid(type::IntTy)) {
      errormsg->Error(pos_, "integer required");
      return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                              type::VoidTy::Instance());
    }
    if (this->oper_ == AND_OP || this->oper_ == OR_OP) {
      tr::Cx left_cx = left_exp_ty->exp_->UnCx(errormsg);
      tr::Cx right_cx = right_exp_ty->exp_->UnCx(errormsg);
      temp::Temp *result_temp = temp::TempFactory::NewTemp();
      temp::Label *eval_right_label = temp::LabelFactory::NewLabel();
      temp::Label *true_label = temp::LabelFactory::NewLabel();
      temp::Label *false_label = temp::LabelFactory::NewLabel();
      temp::Label *done_label = temp::LabelFactory::NewLabel();

      if (this->oper_ == AND_OP) {
        left_cx.trues_.DoPatch(eval_right_label);
        left_cx.falses_.DoPatch(false_label);
      } else {
        left_cx.trues_.DoPatch(true_label);
        left_cx.falses_.DoPatch(eval_right_label);
      }
      right_cx.trues_.DoPatch(true_label);
      right_cx.falses_.DoPatch(false_label);

      tree::Stm *stm = new tree::SeqStm(
          left_cx.stm_,
          new tree::SeqStm(
              new tree::LabelStm(eval_right_label),
              new tree::SeqStm(
                  right_cx.stm_,
                  new tree::SeqStm(
                      new tree::LabelStm(true_label),
                      new tree::SeqStm(
                          new tree::MoveStm(new tree::TempExp(result_temp),
                                            new tree::ConstExp(1)),
                          new tree::SeqStm(
                              new tree::JumpStm(
                                  new tree::NameExp(done_label),
                                  new std::vector<temp::Label *>{done_label}),
                              new tree::SeqStm(
                                  new tree::LabelStm(false_label),
                                  new tree::SeqStm(
                                      new tree::MoveStm(
                                          new tree::TempExp(result_temp),
                                          new tree::ConstExp(0)),
                                      new tree::LabelStm(done_label)))))))));
      return new tr::ExpAndTy(
          new tr::ExExp(new tree::EseqExp(stm, new tree::TempExp(result_temp))),
          type::IntTy::Instance());
    }
    tree::BinOp bin_op;
    switch (this->oper_) {
    case PLUS_OP:
      bin_op = tree::PLUS_OP;
      break;
    case MINUS_OP:
      bin_op = tree::MINUS_OP;
      break;
    case TIMES_OP:
      bin_op = tree::MUL_OP;
      break;
    case DIVIDE_OP:
      bin_op = tree::DIV_OP;
      break;
    default:
      assert(0);
    }
    return new tr::ExpAndTy(
        new tr::ExExp(new tree::BinopExp(bin_op, left_exp, right_exp)),
        type::IntTy::Instance());
  }
  // Rel ops
  case EQ_OP:
  case NEQ_OP:
  case LT_OP:
  case LE_OP:
  case GT_OP:
  case GE_OP: {
    if (this->oper_ == EQ_OP || this->oper_ == NEQ_OP) {
      if (!left_ty->IsSameType(right_ty)) {
        errormsg->Error(pos_, "same type required");
        return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                                type::VoidTy::Instance());
      }
    } else {
      bool left_is_ordered = typeid(*left_ty) == typeid(type::IntTy) ||
                             typeid(*left_ty) == typeid(type::StringTy);
      bool right_is_ordered = typeid(*right_ty) == typeid(type::IntTy) ||
                              typeid(*right_ty) == typeid(type::StringTy);
      if (!left_is_ordered || !right_is_ordered ||
          !left_ty->IsSameType(right_ty)) {
        errormsg->Error(pos_, "same type required");
        return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                                type::VoidTy::Instance());
      }
    }
    if ((this->oper_ == EQ_OP || this->oper_ == NEQ_OP) &&
        typeid(*left_ty) == typeid(type::StringTy)) {
      auto *args = new tree::ExpList({left_exp, right_exp});
      tree::Exp *eq = frame::ExternalCall("string_equal", args);
      auto *cj =
          new tree::CjumpStm(this->oper_ == EQ_OP ? tree::NE_OP : tree::EQ_OP,
                             eq, new tree::ConstExp(0), nullptr, nullptr);
      return new tr::ExpAndTy(new tr::CxExp(tr::PatchList({&cj->true_label_}),
                                            tr::PatchList({&cj->false_label_}),
                                            cj),
                              type::IntTy::Instance());
    }
    tree::RelOp rel_op;
    switch (this->oper_) {
    case EQ_OP:
      rel_op = tree::EQ_OP;
      break;
    case NEQ_OP:
      rel_op = tree::NE_OP;
      break;
    case LT_OP:
      rel_op = tree::LT_OP;
      break;
    case LE_OP:
      rel_op = tree::LE_OP;
      break;
    case GT_OP:
      rel_op = tree::GT_OP;
      break;
    case GE_OP:
      rel_op = tree::GE_OP;
      break;
    default:
      assert(0);
    }
    auto *cj =
        new tree::CjumpStm(rel_op, left_exp, right_exp, nullptr, nullptr);
    return new tr::ExpAndTy(new tr::CxExp(tr::PatchList({&cj->true_label_}),
                                          tr::PatchList({&cj->false_label_}),
                                          cj),
                            type::IntTy::Instance());
  }
  default:
    assert(0);
  }
  return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                          type::VoidTy::Instance());
}

tr::ExpAndTy *RecordExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   tr::Level *level, temp::Label *label,
                                   err::ErrorMsg *errormsg) const {
  // eg: {field1=exp1, field2=exp2, ...} -> ESEQ(MOVE(TEMP record_temp,
  // CALL(NAME alloc_record, CONST field_num * word_size)),
  // SEQ(MOVE(MEM(BINOP(PLUS, TEMP record_temp, CONST 0)), exp1),
  // SEQ(MOVE(MEM(BINOP(PLUS, TEMP record_temp, CONST word_size)), exp2), ...)))
  type::Ty *ty = tenv->Look(typ_);
  if (!ty) {
    errormsg->Error(pos_, "undefined type %s", typ_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto *record_ty = dynamic_cast<type::RecordTy *>(ty->ActualTy());
  if (!record_ty) {
    errormsg->Error(pos_, "not a record type");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }

  auto *record_temp = temp::TempFactory::NewTemp();
  auto *record_exp = new tree::TempExp(record_temp);
  auto *field_num = new tree::ConstExp(record_ty->fields_->GetList().size() *
                                       reg_manager->WordSize());
  auto *init_record =
      frame::ExternalCall("alloc_record", new tree::ExpList({field_num}));
  tree::Stm *stm = new tree::MoveStm(record_exp, init_record);

  int order = 0;
  auto def_it = record_ty->fields_->GetList().begin();
  auto actual_it = fields_->GetList().begin();
  for (; def_it != record_ty->fields_->GetList().end() &&
         actual_it != fields_->GetList().end();
       ++def_it, ++actual_it, ++order) {
    tree::Exp *field_exp =
        (*actual_it)
            ->exp_->Translate(venv, tenv, level, label, errormsg)
            ->exp_->UnEx();
    tree::Exp *field_dst = new tree::MemExp(new tree::BinopExp(
        tree::PLUS_OP, new tree::TempExp(record_temp),
        new tree::ConstExp(order * reg_manager->WordSize())));
    tree::Stm *move = new tree::MoveStm(field_dst, field_exp);
    stm = stm ? static_cast<tree::Stm *>(new tree::SeqStm(stm, move)) : move;
  }
  return new tr::ExpAndTy(
      new tr::ExExp(new tree::EseqExp(stm, new tree::TempExp(record_temp))),
      ty->ActualTy());
}

tr::ExpAndTy *SeqExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: exp1; exp2; ... -> ESEQ(exp1, ESEQ(exp2, ...))
  if (!seq_ || seq_->GetList().empty()) {
    return new tr::ExpAndTy(
        new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
        type::VoidTy::Instance());
  }

  tr::ExpAndTy *result = nullptr;
  tree::Stm *stm = nullptr;
  auto it = seq_->GetList().begin();
  for (; it != seq_->GetList().end(); ++it) {
    result = (*it)->Translate(venv, tenv, level, label, errormsg);
    if (std::next(it) != seq_->GetList().end()) {
      tree::Stm *curr = result->exp_->UnNx();
      stm = stm ? new tree::SeqStm(stm, curr) : curr;
    }
  }

  if (!result) {
    return new tr::ExpAndTy(
        new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
        type::VoidTy::Instance());
  }

  if (dynamic_cast<type::VoidTy *>(result->ty_->ActualTy())) {
    tree::Stm *final_stm = result->exp_->UnNx();
    stm = stm ? new tree::SeqStm(stm, final_stm) : final_stm;
    return new tr::ExpAndTy(
        new tr::NxExp(stm ? stm : new tree::ExpStm(new tree::ConstExp(0))),
        type::VoidTy::Instance());
  }

  return new tr::ExpAndTy(
      new tr::ExExp(
          new tree::EseqExp(stm ? stm : new tree::ExpStm(new tree::ConstExp(0)),
                            result->exp_->UnEx())),
      result->ty_->ActualTy());
}

tr::ExpAndTy *AssignExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   tr::Level *level, temp::Label *label,
                                   err::ErrorMsg *errormsg) const {
  // eg: var := exp -> MOVE(var, exp)
  tr::ExpAndTy *var_exp_ty =
      var_->Translate(venv, tenv, level, label, errormsg);
  tr::ExpAndTy *exp_exp_ty =
      exp_->Translate(venv, tenv, level, label, errormsg);
  return new tr::ExpAndTy(
      new tr::NxExp(new tree::MoveStm(var_exp_ty->exp_->UnEx(),
                                      exp_exp_ty->exp_->UnEx())),
      type::VoidTy::Instance());
}

tr::ExpAndTy *IfExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                               tr::Level *level, temp::Label *label,
                               err::ErrorMsg *errormsg) const {
  // eg: if (test) then then_exp else else_exp -> if test then then_exp else
  // else_exp
  tr::ExpAndTy *test_exp_ty =
      test_->Translate(venv, tenv, level, label, errormsg);
  tr::Cx test_cx = test_exp_ty->exp_->UnCx(errormsg);
  temp::Label *then_label = temp::LabelFactory::NewLabel();
  temp::Label *done_label = temp::LabelFactory::NewLabel();
  temp::Label *else_label =
      elsee_ ? temp::LabelFactory::NewLabel() : done_label;
  test_cx.trues_.DoPatch(then_label);
  test_cx.falses_.DoPatch(else_label);

  tr::ExpAndTy *then_exp_ty =
      then_->Translate(venv, tenv, level, label, errormsg);

  if (!elsee_) {
    tree::Stm *stm = new tree::SeqStm(
        test_cx.stm_,
        new tree::SeqStm(new tree::LabelStm(then_label),
                         new tree::SeqStm(then_exp_ty->exp_->UnNx(),
                                          new tree::LabelStm(done_label))));
    return new tr::ExpAndTy(new tr::NxExp(stm), type::VoidTy::Instance());
  }

  tr::ExpAndTy *else_exp_ty =
      elsee_->Translate(venv, tenv, level, label, errormsg);
  if (dynamic_cast<type::VoidTy *>(then_exp_ty->ty_->ActualTy()) &&
      dynamic_cast<type::VoidTy *>(else_exp_ty->ty_->ActualTy())) {
    tree::Stm *stm = new tree::SeqStm(
        test_cx.stm_,
        new tree::SeqStm(
            new tree::LabelStm(then_label),
            new tree::SeqStm(
                then_exp_ty->exp_->UnNx(),
                new tree::SeqStm(
                    new tree::JumpStm(
                        new tree::NameExp(done_label),
                        new std::vector<temp::Label *>{done_label}),
                    new tree::SeqStm(
                        new tree::LabelStm(else_label),
                        new tree::SeqStm(else_exp_ty->exp_->UnNx(),
                                         new tree::LabelStm(done_label)))))));
    return new tr::ExpAndTy(new tr::NxExp(stm), type::VoidTy::Instance());
  }

  temp::Temp *result_temp = temp::TempFactory::NewTemp();
  tree::Stm *stm = new tree::SeqStm(
      test_cx.stm_,
      new tree::SeqStm(
          new tree::LabelStm(then_label),
          new tree::SeqStm(
              new tree::MoveStm(new tree::TempExp(result_temp),
                                then_exp_ty->exp_->UnEx()),
              new tree::SeqStm(
                  new tree::JumpStm(new tree::NameExp(done_label),
                                    new std::vector<temp::Label *>{done_label}),
                  new tree::SeqStm(
                      new tree::LabelStm(else_label),
                      new tree::SeqStm(
                          new tree::MoveStm(new tree::TempExp(result_temp),
                                            else_exp_ty->exp_->UnEx()),
                          new tree::LabelStm(done_label)))))));
  return new tr::ExpAndTy(
      new tr::ExExp(new tree::EseqExp(stm, new tree::TempExp(result_temp))),
      then_exp_ty->ty_->ActualTy());
}

tr::ExpAndTy *WhileExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                  tr::Level *level, temp::Label *label,
                                  err::ErrorMsg *errormsg) const {
  // eg: while (test) do body -> LABEL test_label: test; CJUMP(test, body_label,
  // done_label); LABEL body_label: body; JUMP(test_label); LABEL done_label:
  temp::Label *test_label = temp::LabelFactory::NewLabel();
  temp::Label *body_label = temp::LabelFactory::NewLabel();
  temp::Label *done_label = temp::LabelFactory::NewLabel();
  tr::ExpAndTy *test_exp_ty =
      test_->Translate(venv, tenv, level, label, errormsg);
  tr::Cx test_cx = test_exp_ty->exp_->UnCx(errormsg);
  test_cx.trues_.DoPatch(body_label);
  test_cx.falses_.DoPatch(done_label);
  tr::ExpAndTy *body_exp_ty =
      body_->Translate(venv, tenv, level, done_label, errormsg);
  tree::Stm *stm = new tree::SeqStm(
      new tree::LabelStm(test_label),
      new tree::SeqStm(
          test_cx.stm_,
          new tree::SeqStm(
              new tree::LabelStm(body_label),
              new tree::SeqStm(
                  body_exp_ty->exp_->UnNx(),
                  new tree::SeqStm(
                      new tree::JumpStm(
                          new tree::NameExp(test_label),
                          new std::vector<temp::Label *>{test_label}),
                      new tree::LabelStm(done_label))))));
  return new tr::ExpAndTy(new tr::NxExp(stm), type::VoidTy::Instance());
}

tr::ExpAndTy *ForExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: for var := lo to hi do body -> LABEL test_label: var := lo; CJUMP(var
  // <= hi, body_label, done_label); LABEL body_label: body; var := var + 1;
  // JUMP(test_label); LABEL done_label:
  temp::Label *test_label = temp::LabelFactory::NewLabel();
  temp::Label *body_label = temp::LabelFactory::NewLabel();
  temp::Label *done_label = temp::LabelFactory::NewLabel();
  tr::ExpAndTy *lo_exp_ty = lo_->Translate(venv, tenv, level, label, errormsg);
  tr::ExpAndTy *hi_exp_ty = hi_->Translate(venv, tenv, level, label, errormsg);
  tr::Access *access = tr::Access::AllocLocal(level, escape_);
  venv->BeginScope();
  venv->Enter(var_, new env::VarEntry(access, type::IntTy::Instance(), true));
  temp::Temp *hi_temp = temp::TempFactory::NewTemp();
  tr::ExpAndTy *body_exp_ty =
      body_->Translate(venv, tenv, level, done_label, errormsg);
  venv->EndScope();

  auto make_var_exp = [&]() {
    return access->access_->ToExp(
        new tree::TempExp(reg_manager->FramePointer()));
  };
  auto make_hi_exp = [&]() { return new tree::TempExp(hi_temp); };

  tree::Stm *init_stm =
      new tree::MoveStm(make_var_exp(), lo_exp_ty->exp_->UnEx());
  tree::Stm *save_hi_stm =
      new tree::MoveStm(make_hi_exp(), hi_exp_ty->exp_->UnEx());
  tree::Stm *loop_test = new tree::CjumpStm(
      tree::LE_OP, make_var_exp(), make_hi_exp(), body_label, done_label);
  tree::Stm *inc_stm = new tree::MoveStm(
      make_var_exp(),
      new tree::BinopExp(tree::PLUS_OP, make_var_exp(), new tree::ConstExp(1)));
  tree::Stm *stm = new tree::SeqStm(
      init_stm,
      new tree::SeqStm(
          save_hi_stm,
          new tree::SeqStm(
              new tree::LabelStm(test_label),
              new tree::SeqStm(
                  loop_test,
                  new tree::SeqStm(
                      new tree::LabelStm(body_label),
                      new tree::SeqStm(
                          body_exp_ty->exp_->UnNx(),
                          new tree::SeqStm(
                              inc_stm,
                              new tree::SeqStm(
                                  new tree::JumpStm(
                                      new tree::NameExp(test_label),
                                      new std::vector<temp::Label *>{
                                          test_label}),
                                  new tree::LabelStm(done_label)))))))));
  return new tr::ExpAndTy(new tr::NxExp(stm), type::VoidTy::Instance());
}

tr::ExpAndTy *BreakExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                  tr::Level *level, temp::Label *label,
                                  err::ErrorMsg *errormsg) const {
  // eg: break -> JUMP(break_label)
  if (!label) {
    errormsg->Error(pos_, "break outside any loop");
    return new tr::ExpAndTy(
        new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
        type::VoidTy::Instance());
  }
  return new tr::ExpAndTy(
      new tr::NxExp(new tree::JumpStm(new tree::NameExp(label),
                                      new std::vector<temp::Label *>{label})),
      type::VoidTy::Instance());
}

tr::ExpAndTy *LetExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: let decs in body end -> let decs in body end
  venv->BeginScope();
  tenv->BeginScope();

  tree::Stm *dec_stm = nullptr;
  if (decs_) {
    for (auto dec : decs_->GetList()) {
      tr::Exp *dec_exp = dec->Translate(venv, tenv, level, label, errormsg);
      if (dec_exp) {
        tree::Stm *curr = dec_exp->UnNx();
        dec_stm = dec_stm ? new tree::SeqStm(dec_stm, curr) : curr;
      }
    }
  }

  tr::ExpAndTy *body_exp_ty =
      body_ ? body_->Translate(venv, tenv, level, label, errormsg)
            : new tr::ExpAndTy(
                  new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
                  type::VoidTy::Instance());

  tenv->EndScope();
  venv->EndScope();

  if (dynamic_cast<type::VoidTy *>(body_exp_ty->ty_->ActualTy())) {
    tree::Stm *body_stm = body_exp_ty->exp_->UnNx();
    tree::Stm *combined =
        dec_stm ? new tree::SeqStm(dec_stm, body_stm) : body_stm;
    return new tr::ExpAndTy(new tr::NxExp(combined), type::VoidTy::Instance());
  }
  return new tr::ExpAndTy(
      new tr::ExExp(new tree::EseqExp(
          dec_stm ? dec_stm : new tree::ExpStm(new tree::ConstExp(0)),
          body_exp_ty->exp_->UnEx())),
      body_exp_ty->ty_->ActualTy());
}

tr::ExpAndTy *ArrayExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                  tr::Level *level, temp::Label *label,
                                  err::ErrorMsg *errormsg) const {
  // eg: array[exp1] of exp2 -> CALL(NAME init_array, exp1, exp2)
  type::Ty *ty = tenv->Look(typ_);
  if (!ty) {
    errormsg->Error(pos_, "undefined type %s", typ_->Name().data());
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  auto *array_ty = dynamic_cast<type::ArrayTy *>(ty->ActualTy());
  if (!array_ty) {
    errormsg->Error(pos_, "not an array type");
    return new tr::ExpAndTy(new tr::ExExp(new tree::ConstExp(0)),
                            type::VoidTy::Instance());
  }
  tr::ExpAndTy *size_exp_ty =
      size_->Translate(venv, tenv, level, label, errormsg);
  tr::ExpAndTy *init_exp_ty =
      init_->Translate(venv, tenv, level, label, errormsg);
  auto *args = new tree::ExpList();
  args->Append(size_exp_ty->exp_->UnEx());
  args->Append(init_exp_ty->exp_->UnEx());
  return new tr::ExpAndTy(
      new tr::ExExp(frame::ExternalCall("init_array", args)),
      array_ty->ActualTy());
}

tr::ExpAndTy *VoidExp::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                 tr::Level *level, temp::Label *label,
                                 err::ErrorMsg *errormsg) const {
  // eg: () -> no expression, just return void type
  return new tr::ExpAndTy(
      new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
      type::VoidTy::Instance());
}

tr::Exp *FunctionDec::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                                tr::Level *level, temp::Label *label,
                                err::ErrorMsg *errormsg) const {
  // eg: function f(params) : result = body -> LABEL f_label: body
  if (!functions_) {
    return nullptr;
  }

  struct FunInfo {
    absyn::FunDec *fun_dec;
    tr::Level *level;
    type::Ty *result_ty;
    type::TyList *formal_tys;
  };

  std::vector<FunInfo> batch;
  batch.reserve(functions_->GetList().size());

  for (auto fun_dec : functions_->GetList()) {
    type::TyList *formal_tys =
        fun_dec->params_->MakeFormalTyList(tenv, errormsg);
    std::list<bool> escapes;
    for (auto field : fun_dec->params_->GetList()) {
      escapes.push_back(field->escape_);
    }
    temp::Label *fun_label =
        temp::LabelFactory::NamedLabel(fun_dec->name_->Name());
    type::Ty *result_ty = type::VoidTy::Instance();
    if (fun_dec->result_) {
      result_ty = tenv->Look(fun_dec->result_);
      if (!result_ty) {
        errormsg->Error(fun_dec->pos_, "undefined type %s",
                        fun_dec->result_->Name().data());
        result_ty = type::VoidTy::Instance();
      }
    }
    tr::Level *fun_level = tr::Level::NewLevel(level, fun_label, escapes);
    venv->Enter(fun_dec->name_,
                new env::FunEntry(fun_level, fun_label, formal_tys, result_ty));
    batch.push_back({fun_dec, fun_level, result_ty, formal_tys});
  }

  for (auto &item : batch) {
    venv->BeginScope();
    auto formal_accesses = item.level->Formals();
    tree::Stm *view_shift = nullptr;
    auto arg_regs = reg_manager->ArgRegs();
    auto arg_reg_it = arg_regs->GetList().begin();
    int arg_index = 0;
    for (auto formal_access : *formal_accesses) {
      tree::Exp *frame_ptr = new tree::TempExp(reg_manager->FramePointer());
      tree::Exp *src = nullptr;
      if (arg_index < static_cast<int>(arg_regs->GetList().size()) &&
          arg_reg_it != arg_regs->GetList().end()) {
        src = new tree::TempExp(*arg_reg_it++);
      } else {
        int stack_index =
            arg_index - static_cast<int>(arg_regs->GetList().size());
        src = new tree::MemExp(new tree::BinopExp(
            tree::PLUS_OP, new tree::TempExp(reg_manager->FramePointer()),
            new tree::ConstExp(16 + stack_index * reg_manager->WordSize())));
      }
      tree::Stm *move =
          new tree::MoveStm(formal_access->access_->ToExp(frame_ptr), src);
      view_shift = view_shift ? new tree::SeqStm(view_shift, move) : move;
      ++arg_index;
    }
    item.level->frame_->SetViewShift(view_shift);

    auto formal_it = formal_accesses->begin();
    if (formal_it != formal_accesses->end()) {
      ++formal_it;
    }
    auto field_it = item.fun_dec->params_->GetList().begin();
    auto formal_ty_it = item.formal_tys->GetList().begin();
    for (; field_it != item.fun_dec->params_->GetList().end() &&
           formal_it != formal_accesses->end() &&
           formal_ty_it != item.formal_tys->GetList().end();
         ++field_it, ++formal_it, ++formal_ty_it) {
      venv->Enter((*field_it)->name_,
                  new env::VarEntry(*formal_it, (*formal_ty_it)->ActualTy()));
    }

    tr::ExpAndTy *body_exp_ty =
        item.fun_dec->body_
            ? item.fun_dec->body_->Translate(venv, tenv, item.level, nullptr,
                                             errormsg)
            : new tr::ExpAndTy(
                  new tr::NxExp(new tree::ExpStm(new tree::ConstExp(0))),
                  type::VoidTy::Instance());
    venv->EndScope();
    frags->PushBack(ProcEntryExit(item.level, body_exp_ty->exp_));
  }
  return nullptr;
}

tr::Exp *VarDec::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                           tr::Level *level, temp::Label *label,
                           err::ErrorMsg *errormsg) const {
  // eg: var var := init -> MOVE(var, init)
  tr::ExpAndTy *init_exp_ty =
      init_->Translate(venv, tenv, level, label, errormsg);
  type::Ty *init_ty = init_exp_ty->ty_;
  type::Ty *var_ty = init_ty->ActualTy();

  if (typ_) {
    type::Ty *ty = tenv->Look(typ_);
    if (!ty) {
      errormsg->Error(pos_, "undefined type %s", typ_->Name().data());
      ty = type::VoidTy::Instance();
    }

    if (!ty->IsSameType(init_ty)) {
      errormsg->Error(pos_, "type and init type mismatch");
    }
    var_ty = ty->ActualTy();
  } else {
    auto actual_init_ty = init_ty->ActualTy();
    if (typeid(*actual_init_ty) == typeid(type::NilTy)) {
      errormsg->Error(pos_, "init should not be nil without type specified");
    }
    var_ty = actual_init_ty;
  }

  tr::Access *access = tr::Access::AllocLocal(level, escape_);
  venv->Enter(var_, new env::VarEntry(access, var_ty));

  return new tr::NxExp(new tree::MoveStm(
      access->access_->ToExp(new tree::TempExp(reg_manager->FramePointer())),
      init_exp_ty->exp_->UnEx()));
}

tr::Exp *TypeDec::Translate(env::VEnvPtr venv, env::TEnvPtr tenv,
                            tr::Level *level, temp::Label *label,
                            err::ErrorMsg *errormsg) const {
  // eg: type ty = ty_def -> no expression, just add ty to tenv
  if (!types_) {
    return nullptr;
  }
  std::vector<std::pair<absyn::NameAndTy *, type::NameTy *>> batch;
  batch.reserve(types_->GetList().size());
  for (auto name_and_ty : types_->GetList()) {
    auto *placeholder = new type::NameTy(name_and_ty->name_, nullptr);
    tenv->Enter(name_and_ty->name_, placeholder);
    batch.emplace_back(name_and_ty, placeholder);
  }
  for (auto &item : batch) {
    item.second->ty_ = item.first->ty_->Translate(tenv, errormsg);
  }
  return nullptr;
}

type::Ty *NameTy::Translate(env::TEnvPtr tenv, err::ErrorMsg *errormsg) const {
  // eg: type name -> look up the name in tenv and return the type
  type::Ty *ty = tenv->Look(name_);
  if (!ty) {
    errormsg->Error(pos_, "undefined type %s", name_->Name().data());
    return type::VoidTy::Instance();
  }
  return ty;
}

type::Ty *RecordTy::Translate(env::TEnvPtr tenv,
                              err::ErrorMsg *errormsg) const {
  // eg: {field1: ty1, field2: ty2, ...} -> record type with fields field1,
  // field2, ...
  auto *field_list = new type::FieldList();
  if (!record_) {
    return new type::RecordTy(field_list);
  }
  for (auto field : record_->GetList()) {
    type::Ty *ty = tenv->Look(field->typ_);
    if (!ty) {
      errormsg->Error(pos_, "undefined type %s", field->typ_->Name().data());
      ty = type::VoidTy::Instance();
    }
    field_list->Append(new type::Field(field->name_, ty));
  }
  return new type::RecordTy(field_list);
}

type::Ty *ArrayTy::Translate(env::TEnvPtr tenv, err::ErrorMsg *errormsg) const {
  // eg: array of ty -> array type with element type ty
  type::Ty *ty = tenv->Look(array_);
  if (!ty) {
    errormsg->Error(pos_, "undefined type %s", array_->Name().data());
    return type::VoidTy::Instance();
  }
  return new type::ArrayTy(ty);
}

} // namespace absyn
