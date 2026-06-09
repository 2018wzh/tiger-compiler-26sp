#include "tiger/semant/semant.h"
#include "tiger/absyn/absyn.h"
#include "tiger/semant/types.h"
#include <unordered_set>
#include <vector>

namespace absyn {

bool CheckTypeCycle(type::Ty *ty, std::unordered_set<type::NameTy *> *seen) {
  if (ty == nullptr)
    return false;
  if (dynamic_cast<type::RecordTy *>(ty) || dynamic_cast<type::ArrayTy *>(ty))
    return false;
  auto *name_ty = dynamic_cast<type::NameTy *>(ty);
  if (name_ty == nullptr)
    return false;
  if (seen->count(name_ty) != 0)
    return true; // Cycle detected.
  seen->insert(name_ty);
  return CheckTypeCycle(name_ty->ty_, seen);
}

void AbsynTree::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                           err::ErrorMsg *errormsg) const {
  if (this->root_) {
    this->root_->SemAnalyze(venv, tenv, 0, errormsg);
  } else {
    errormsg->Error(0, "no expression");
  }
}

type::Ty *SimpleVar::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                                int labelcount, err::ErrorMsg *errormsg) const {
  auto *entry = dynamic_cast<env::VarEntry *>(venv->Look(this->sym_));
  if (!entry) {
    // undefined var
    errormsg->Error(this->pos_, "undefined variable %s",
                    this->sym_->Name().c_str());
    return type::VoidTy::Instance();
  }
  return entry->ty_;
}

type::Ty *FieldVar::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                               int labelcount, err::ErrorMsg *errormsg) const {
  type::Ty *parent_ty =
      this->var_->SemAnalyze(venv, tenv, labelcount, errormsg);
  auto *record_ty = dynamic_cast<type::RecordTy *>(parent_ty->ActualTy());
  if (!record_ty) {
    // not a record type, eg. a string or an array
    errormsg->Error(this->pos_, "not a record type");
    return type::VoidTy::Instance();
  }
  // Look up the field in the record type.
  for (auto *field : record_ty->fields_->GetList())
    if (field->name_ == this->sym_)
      return field->ty_;
  // field not found
  errormsg->Error(this->pos_, "field %s doesn't exist",
                  this->sym_->Name().c_str());
  return type::VoidTy::Instance();
}

type::Ty *SubscriptVar::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                                   int labelcount,
                                   err::ErrorMsg *errormsg) const {
  type::Ty *parent_ty =
      this->var_->SemAnalyze(venv, tenv, labelcount, errormsg);
  auto *array_ty = dynamic_cast<type::ArrayTy *>(parent_ty->ActualTy());
  if (!array_ty) {
    // not an array type, eg. a string or a record
    errormsg->Error(this->pos_, "array type required");
    return type::VoidTy::Instance();
  }
  // Look up the subscript type in the array type.
  type::Ty *subscript_ty =
      this->subscript_->SemAnalyze(venv, tenv, labelcount, errormsg);
  if (!subscript_ty->IsSameType(type::IntTy::Instance())) {
    // subscript is not an integer
    errormsg->Error(this->subscript_->pos_, "integer required");
    return type::VoidTy::Instance();
  }
  return array_ty->ty_;
}

type::Ty *VarExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  return this->var_->SemAnalyze(venv, tenv, labelcount, errormsg);
}

type::Ty *NilExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  return type::NilTy::Instance();
}

type::Ty *IntExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  return type::IntTy::Instance();
}

type::Ty *StringExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                                int labelcount, err::ErrorMsg *errormsg) const {
  return type::StringTy::Instance();
}

type::Ty *CallExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                              int labelcount, err::ErrorMsg *errormsg) const {
  auto *func_entry = dynamic_cast<env::FunEntry *>(venv->Look(this->func_));
  if (!func_entry) {
    // not defined
    errormsg->Error(this->pos_, "undefined function %s",
                    this->func_->Name().c_str());
    return type::VoidTy::Instance();
  }
  const auto &formals = func_entry->formals_->GetList();
  const auto &actuals = args_->GetList();
  // Check the number of parameters.
  if (actuals.size() < formals.size()) {
    errormsg->Error(this->pos_, "too few params in function %s",
                    this->func_->Name().c_str());
  } else if (actuals.size() > formals.size()) {
    errormsg->Error(this->pos_, "too many params in function %s",
                    this->func_->Name().c_str());
  }
  auto formal_it = formals.begin();
  auto actual_it = actuals.begin();
  bool mismatch = false;
  bool arg_had_error = false;
  for (; formal_it != formals.end() && actual_it != actuals.end();
       ++formal_it, ++actual_it) {
    bool arg_error_before = errormsg->AnyErrors();
    type::Ty *actual_ty =
        (*actual_it)->SemAnalyze(venv, tenv, labelcount, errormsg);
    if (errormsg->AnyErrors() != arg_error_before) {
      arg_had_error = true;
    }
    if (!arg_had_error && !(*formal_it)->IsSameType(actual_ty)) {
      mismatch = true;
    }
  }
  if (mismatch && !arg_had_error) {
    errormsg->Error(this->pos_, "para type mismatch");
  }
  return func_entry->result_;
}

type::Ty *OpExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                            int labelcount, err::ErrorMsg *errormsg) const {
  bool left_error_before = errormsg->AnyErrors();
  type::Ty *left_ty = this->left_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool left_had_error = errormsg->AnyErrors() && !left_error_before;
  bool right_error_before = errormsg->AnyErrors();
  type::Ty *right_ty =
      this->right_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool right_had_error = errormsg->AnyErrors() && !right_error_before;
  if (left_had_error || right_had_error) {
    return type::VoidTy::Instance();
  }
  switch (this->oper_) {
    // integer operations: +, -, *, /
  case PLUS_OP:
  case MINUS_OP:
  case TIMES_OP:
  case DIVIDE_OP:
    if (!left_ty->IsSameType(type::IntTy::Instance()) ||
        !right_ty->IsSameType(type::IntTy::Instance())) {
      errormsg->Error(this->pos_, "integer required");
      return type::VoidTy::Instance();
    }
    return type::IntTy::Instance();
    // logical operations: &, |
  case AND_OP:
  case OR_OP:
    if (!left_ty->IsSameType(type::IntTy::Instance()) ||
        !right_ty->IsSameType(type::IntTy::Instance())) {
      errormsg->Error(this->pos_, "integer required");
      return type::VoidTy::Instance();
    }
    return type::IntTy::Instance();
    // equality and inequality: =, <>
  case EQ_OP:
  case NEQ_OP:
    if (!left_ty->IsSameType(right_ty)) {
      errormsg->Error(this->pos_, "same type required");
      return type::VoidTy::Instance();
    }
    return type::IntTy::Instance();
    // comparison: <, <=, >, >=. Only int and string are allowed.
  case LT_OP:
  case LE_OP:
  case GT_OP:
  case GE_OP:
    if ((!left_ty->IsSameType(type::IntTy::Instance()) &&
         !left_ty->IsSameType(type::StringTy::Instance())) ||
        (!right_ty->IsSameType(type::IntTy::Instance()) &&
         !right_ty->IsSameType(type::StringTy::Instance())) ||
        !left_ty->IsSameType(right_ty)) {
      errormsg->Error(this->pos_, "same type required");
      return type::VoidTy::Instance();
    }
    return type::IntTy::Instance();
    // should not reach here
  default:
    assert(false);
    return type::VoidTy::Instance();
  }
}

type::Ty *RecordExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                                int labelcount, err::ErrorMsg *errormsg) const {
  type::Ty *ty = tenv->Look(this->typ_);
  if (!ty) {
    errormsg->Error(this->pos_, "undefined type %s",
                    this->typ_->Name().c_str());
    return type::VoidTy::Instance();
  }
  auto *record_ty = dynamic_cast<type::RecordTy *>(ty->ActualTy());
  if (!record_ty) {
    errormsg->Error(this->pos_, "not a record type");
    return type::VoidTy::Instance();
  }

  const auto &defs = record_ty->fields_->GetList();
  const auto &actuals = fields_->GetList();

  if (defs.size() != actuals.size()) {
    if (actuals.empty()) {
      errormsg->Error(this->pos_, "field %s doesn't exist",
                      this->typ_->Name().c_str());
    } else {
      errormsg->Error(this->pos_, "field %s doesn't exist",
                      (*actuals.begin())->name_->Name().c_str());
    }
  }
  // Check the name and type of each field.
  auto def_it = defs.begin();
  auto actual_it = actuals.begin();
  for (; def_it != defs.end() && actual_it != actuals.end();
       ++def_it, ++actual_it) {
    if ((*def_it)->name_ != (*actual_it)->name_) {
      errormsg->Error((*actual_it)->exp_->pos_, "field %s doesn't exist",
                      (*actual_it)->name_->Name().c_str());
      break;
    }

    bool field_error_before = errormsg->AnyErrors();
    type::Ty *actual_ty =
        (*actual_it)->exp_->SemAnalyze(venv, tenv, labelcount, errormsg);
    bool field_had_error = errormsg->AnyErrors() && !field_error_before;
    if (!field_had_error && !(*def_it)->ty_->IsSameType(actual_ty)) {
      errormsg->Error((*actual_it)->exp_->pos_, "same type required");
      break;
    }
  }

  return record_ty;
}

type::Ty *SeqExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  if (!this->seq_) {
    return type::VoidTy::Instance();
  }
  type::Ty *result = type::VoidTy::Instance();
  // Check each expression in the sequence and return the type of the last one.
  for (auto *exp : this->seq_->GetList()) {
    result = exp->SemAnalyze(venv, tenv, labelcount, errormsg);
  }
  return result;
}

type::Ty *IfExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                            int labelcount, err::ErrorMsg *errormsg) const {
  bool test_error_before = errormsg->AnyErrors();
  type::Ty *test_ty = this->test_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool test_had_error = errormsg->AnyErrors() && !test_error_before;
  // test expression should be an integer (0 is false, non-zero is true)
  if (!test_had_error && !test_ty->IsSameType(type::IntTy::Instance())) {
    errormsg->Error(this->test_->pos_, "integer required");
  }
  // get the type of then and else expressions, and check if they match
  // get error status before analyzing then and else expressions, so that we can
  // avoid cascading errors, eg: if the then expression has an error, we won't
  // report error in else expression even if they don't match.
  bool then_error_before = errormsg->AnyErrors();
  type::Ty *then_ty = this->then_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool then_had_error = errormsg->AnyErrors() && !then_error_before;
  if (!this->elsee_) {
    if (!then_had_error && !then_ty->IsSameType(type::VoidTy::Instance())) {
      errormsg->Error(this->pos_, "if-then exp's body must produce no value");
    }
    return type::VoidTy::Instance();
  }
  bool else_error_before = errormsg->AnyErrors();
  type::Ty *else_ty =
      this->elsee_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool else_had_error = errormsg->AnyErrors() && !else_error_before;
  if (test_had_error) {
    return type::VoidTy::Instance();
  }
  if (!then_had_error && !else_had_error && !then_ty->IsSameType(else_ty)) {
    errormsg->Error(this->pos_, "then exp and else exp type mismatch");
    return type::VoidTy::Instance();
  }
  if (then_had_error && !else_had_error) {
    return else_ty->ActualTy();
  }
  if (else_had_error && !then_had_error) {
    return then_ty->ActualTy();
  }
  if (dynamic_cast<type::NilTy *>(then_ty->ActualTy())) {
    return else_ty->ActualTy();
  }
  return then_ty->ActualTy();
}

type::Ty *WhileExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                               int labelcount, err::ErrorMsg *errormsg) const {
  bool test_error_before = errormsg->AnyErrors();
  type::Ty *test_ty = this->test_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool test_had_error = errormsg->AnyErrors() && !test_error_before;
  if (!test_had_error && !test_ty->IsSameType(type::IntTy::Instance())) {
    errormsg->Error(this->test_->pos_, "integer required");
  }
  // get error status before analyzing body expression, so that we can avoid
  // cascading errors, eg: if the body expression has an error, we won't report
  // error in body expression even if it doesn't produce void.
  bool body_error_before = errormsg->AnyErrors();
  type::Ty *body_ty =
      this->body_->SemAnalyze(venv, tenv, labelcount + 1, errormsg);
  bool body_had_error = errormsg->AnyErrors() && !body_error_before;
  if (!body_had_error && !body_ty->IsSameType(type::VoidTy::Instance())) {
    errormsg->Error(this->pos_, "while body must produce no value");
  }

  return type::VoidTy::Instance();
}

type::Ty *BreakExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                               int labelcount, err::ErrorMsg *errormsg) const {
  if (labelcount <= 0) {
    errormsg->Error(this->pos_, "break outside any loop");
  }
  return type::VoidTy::Instance();
}

type::Ty *LetExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  // Create a new scope for the let expression, so that we can handle nested let
  // expressions and avoid name conflicts.
  venv->BeginScope();
  tenv->BeginScope();

  if (this->decs_) {
    for (auto *dec : this->decs_->GetList()) {
      dec->SemAnalyze(venv, tenv, labelcount, errormsg);
    }
  }

  type::Ty *body_ty =
      this->body_ ? this->body_->SemAnalyze(venv, tenv, labelcount, errormsg)
                  : type::VoidTy::Instance();
  tenv->EndScope();
  venv->EndScope();
  return body_ty;
}

type::Ty *ArrayExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                               int labelcount, err::ErrorMsg *errormsg) const {
  type::Ty *ty = tenv->Look(this->typ_);
  if (!ty) {
    errormsg->Error(this->pos_, "undefined type %s",
                    this->typ_->Name().c_str());
    return type::VoidTy::Instance();
  }

  auto *array_ty = dynamic_cast<type::ArrayTy *>(ty->ActualTy());
  if (!array_ty) {
    errormsg->Error(this->pos_, "array type required");
    return type::VoidTy::Instance();
  }

  bool size_error_before = errormsg->AnyErrors();
  type::Ty *size_ty = this->size_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool size_had_error = errormsg->AnyErrors() && !size_error_before;
  if (!size_had_error && !size_ty->IsSameType(type::IntTy::Instance())) {
    errormsg->Error(this->size_->pos_, "integer required");
  }

  bool init_error_before = errormsg->AnyErrors();
  type::Ty *init_ty = this->init_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool init_had_error = errormsg->AnyErrors() && !init_error_before;
  if (!init_had_error && !array_ty->ty_->IsSameType(init_ty)) {
    errormsg->Error(this->init_->pos_, "type mismatch");
  }

  return array_ty;
}

type::Ty *VoidExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                              int labelcount, err::ErrorMsg *errormsg) const {
  return type::VoidTy::Instance();
}

type::Ty *AssignExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                                int labelcount, err::ErrorMsg *errormsg) const {
  // avoid cascading errors by getting error status before analyzing var and
  // expession, so that if var or expression has error, we won't report error in
  // type mismatch.
  type::Ty *var_ty = this->var_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool exp_error_before = errormsg->AnyErrors();
  type::Ty *exp_ty = exp_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool exp_had_error = errormsg->AnyErrors() && !exp_error_before;

  if (auto *simple_var = dynamic_cast<SimpleVar *>(this->var_)) {
    auto *entry = dynamic_cast<env::VarEntry *>(venv->Look(simple_var->sym_));
    if (entry && entry->readonly_) {
      errormsg->Error(this->pos_, "loop variable can't be assigned");
      return type::VoidTy::Instance();
    }
  }

  if (!exp_had_error && !var_ty->IsSameType(exp_ty)) {
    errormsg->Error(this->pos_, "unmatched assign exp");
  }

  return type::VoidTy::Instance();
}

void FunctionDec::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  // First pass: add function headers to the environment, so that we can handle
  // recursive functions. We also check for duplicate function names in this
  // pass.
  if (!this->functions_) {
    return;
  }

  struct FunInfo {
    absyn::FunDec *fun_dec;
    type::TyList *formals;
    type::Ty *result;
  };

  std::unordered_set<sym::Symbol *> names;
  std::vector<FunInfo> batch;
  batch.reserve(this->functions_->GetList().size());

  for (auto *fun_dec : this->functions_->GetList()) {
    if (names.count(fun_dec->name_) != 0) {
      errormsg->Error(fun_dec->pos_, "two functions have the same name");
      continue;
    }
    names.insert(fun_dec->name_);

    type::TyList *formals = fun_dec->params_->MakeFormalTyList(tenv, errormsg);
    type::Ty *result = type::VoidTy::Instance();
    if (fun_dec->result_) {
      result = tenv->Look(fun_dec->result_);
      if (!result) {
        errormsg->Error(fun_dec->pos_, "undefined type %s",
                        fun_dec->result_->Name().c_str());
        result = type::VoidTy::Instance();
      }
    }

    venv->Enter(fun_dec->name_, new env::FunEntry(formals, result));
    batch.push_back({fun_dec, formals, result});
  }
  // Second pass: analyze function bodies and check for type errors. We do this
  // in a second pass because we want to allow mutually recursive functions, so
  // we need to add all function headers to the environment before analyzing any
  // function body.
  for (auto &item : batch) {
    venv->BeginScope();

    auto formal_it = item.formals->GetList().begin();
    for (auto *field : item.fun_dec->params_->GetList()) {
      if (formal_it == item.formals->GetList().end()) {
        break;
      }
      venv->Enter(field->name_, new env::VarEntry(*formal_it));
      ++formal_it;
    }

    bool body_error_before = errormsg->AnyErrors();
    type::Ty *body_ty =
        item.fun_dec->body_
            ? item.fun_dec->body_->SemAnalyze(venv, tenv, labelcount, errormsg)
            : type::VoidTy::Instance();
    bool body_had_error = errormsg->AnyErrors() && !body_error_before;
    venv->EndScope();

    if (item.result == type::VoidTy::Instance()) {
      if (!body_ty->IsSameType(type::VoidTy::Instance())) {
        errormsg->Error(item.fun_dec->pos_, "procedure returns value");
      }
    } else if (!item.result->IsSameType(body_ty)) {
      errormsg->Error(item.fun_dec->pos_, "type mismatch");
    }
  }
}

void VarDec::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv, int labelcount,
                        err::ErrorMsg *errormsg) const {
  type::Ty *init_ty = this->init_->SemAnalyze(venv, tenv, labelcount, errormsg);
  type::Ty *var_ty = nullptr;

  if (this->typ_) {
    var_ty = tenv->Look(this->typ_);
    if (!var_ty) {
      errormsg->Error(this->pos_, "undefined type %s",
                      this->typ_->Name().c_str());
      var_ty = type::VoidTy::Instance();
    }
    if (!errormsg->AnyErrors() && !var_ty->IsSameType(init_ty)) {
      errormsg->Error(this->pos_, "type mismatch");
    }
  } else {
    if (dynamic_cast<type::NilTy *>(init_ty->ActualTy())) {
      // eg: var x := nil. We allow this, but we need to give it a type,
      // otherwise it will cause errors in later type checking. We choose void
      // type here, but it doesn't matter much since nil can be assigned to any
      // record type.
      errormsg->Error(this->pos_,
                      "init should not be nil without type specified");
      var_ty = type::VoidTy::Instance();
    } else {
      var_ty = init_ty->ActualTy();
    }
  }

  venv->Enter(this->var_, new env::VarEntry(var_ty));
}

void TypeDec::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv, int labelcount,
                         err::ErrorMsg *errormsg) const {
  if (!types_) {
    return;
  }
  // First pass: add type headers to the environment, so that we can handle
  // recursive types. We also check for duplicate type names in this pass.
  std::unordered_set<sym::Symbol *> names;
  std::vector<std::pair<absyn::NameAndTy *, type::NameTy *>> batch;
  batch.reserve(types_->GetList().size());

  for (auto *name_and_ty : types_->GetList()) {
    if (names.count(name_and_ty->name_) != 0) {
      errormsg->Error(this->pos_, "two types have the same name");
      continue;
    }
    names.insert(name_and_ty->name_);

    auto *placeholder = new type::NameTy(name_and_ty->name_, nullptr);
    tenv->Enter(name_and_ty->name_, placeholder);
    batch.emplace_back(name_and_ty, placeholder);
  }
  // Second pass: analyze type definitions and check for type errors. We do this
  // in a second pass because we want to allow mutually recursive types, so we
  // need to  add all type headers to the environment before analyzing any type
  // definition.
  // eg: type a = {x: int}; type b = {y: a}; type a = b.
  for (auto &item : batch) {
    item.second->ty_ = item.first->ty_->SemAnalyze(tenv, errormsg);
  }
  // Third pass: check for illegal type cycles. We do this in a third pass
  // because we want to allow legal type cycles
  // eg: type a = {x: a}.
  bool flag = false;
  for (auto &item : batch) {
    std::unordered_set<type::NameTy *> seen;
    if (CheckTypeCycle(item.second, &seen)) {
      item.second->ty_ = type::VoidTy::Instance();
      if (!flag) {
        // Only report error for the first type in the cycle
        errormsg->Error(this->pos_, "illegal type cycle");
        flag = true;
      }
    }
  }
}

type::Ty *NameTy::SemAnalyze(env::TEnvPtr tenv, err::ErrorMsg *errormsg) const {
  type::Ty *ty = tenv->Look(this->name_);
  if (!ty) {
    errormsg->Error(this->pos_, "undefined type %s",
                    this->name_->Name().c_str());
    return type::VoidTy::Instance();
  }
  return ty;
}

type::Ty *RecordTy::SemAnalyze(env::TEnvPtr tenv,
                               err::ErrorMsg *errormsg) const {
  if (!this->record_) {
    return new type::RecordTy(new type::FieldList());
  }
  return new type::RecordTy(this->record_->MakeFieldList(tenv, errormsg));
}

type::Ty *ArrayTy::SemAnalyze(env::TEnvPtr tenv,
                              err::ErrorMsg *errormsg) const {
  type::Ty *ty = tenv->Look(this->array_);
  if (!ty) {
    errormsg->Error(this->pos_, "undefined type %s",
                    this->array_->Name().c_str());
    return type::VoidTy::Instance();
  }
  return new type::ArrayTy(ty);
}

type::Ty *ForExp::SemAnalyze(env::VEnvPtr venv, env::TEnvPtr tenv,
                             int labelcount, err::ErrorMsg *errormsg) const {
  bool low_error_before = errormsg->AnyErrors();
  type::Ty *low_ty = this->lo_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool low_had_error = errormsg->AnyErrors() && !low_error_before;
  bool high_error_before = errormsg->AnyErrors();
  type::Ty *high_ty = this->hi_->SemAnalyze(venv, tenv, labelcount, errormsg);
  bool high_had_error = errormsg->AnyErrors() && !high_error_before;
  if (!low_had_error && !high_had_error &&
      (!low_ty->IsSameType(type::IntTy::Instance()) ||
       !high_ty->IsSameType(type::IntTy::Instance()))) {
    errormsg->Error(this->hi_->pos_, "for exp's range type is not integer");
  }
  // We treat the loop variable as a read-only variable, so that we can catch
  // errors like "for i := 0 to 10 do (i := i + 1)".
  venv->BeginScope();
  venv->Enter(this->var_, new env::VarEntry(type::IntTy::Instance(), true));
  bool body_error_before = errormsg->AnyErrors();
  type::Ty *body_ty =
      this->body_->SemAnalyze(venv, tenv, labelcount + 1, errormsg);
  bool body_had_error = errormsg->AnyErrors() && !body_error_before;
  if (!body_had_error && !body_ty->IsSameType(type::VoidTy::Instance())) {
    errormsg->Error(this->pos_, "for body must produce no value");
  }
  venv->EndScope();

  return type::VoidTy::Instance();
}

} // namespace absyn

namespace sem {

void ProgSem::SemAnalyze() {
  FillBaseVEnv();
  FillBaseTEnv();
  absyn_tree_->SemAnalyze(venv_.get(), tenv_.get(), errormsg_.get());
}

} // namespace sem