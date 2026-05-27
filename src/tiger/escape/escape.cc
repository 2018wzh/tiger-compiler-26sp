#include "tiger/escape/escape.h"
#include "tiger/absyn/absyn.h"
#include "tiger/errormsg/errormsg.h"

namespace esc {
void EscFinder::FindEscape() { absyn_tree_->Traverse(env_.get()); }
} // namespace esc

namespace absyn {

void AbsynTree::Traverse(esc::EscEnvPtr env) {
  if (this->root_)
    this->root_->Traverse(env, 0);
}

void SimpleVar::Traverse(esc::EscEnvPtr env, int depth) {
  auto entry = env->Look(this->sym_);
  if (entry) {
    if (depth > entry->depth_) {
      *(entry->escape_) = true;
    }
  }
}
void FieldVar::Traverse(esc::EscEnvPtr env, int depth) {
  this->var_->Traverse(env, depth);
}

void SubscriptVar::Traverse(esc::EscEnvPtr env, int depth) {
  this->var_->Traverse(env, depth);
  this->subscript_->Traverse(env, depth);
}

void VarExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->var_->Traverse(env, depth);
}

void NilExp::Traverse(esc::EscEnvPtr env, int depth) { return; }

void IntExp::Traverse(esc::EscEnvPtr env, int depth) { return; }

void StringExp::Traverse(esc::EscEnvPtr env, int depth) { return; }

void CallExp::Traverse(esc::EscEnvPtr env, int depth) {
  for (auto exp : this->args_->GetList()) {
    exp->Traverse(env, depth);
  }
}

void OpExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->left_->Traverse(env, depth);
  this->right_->Traverse(env, depth);
}

void RecordExp::Traverse(esc::EscEnvPtr env, int depth) {
  for (auto efield : this->fields_->GetList()) {
    efield->exp_->Traverse(env, depth);
  }
}

void SeqExp::Traverse(esc::EscEnvPtr env, int depth) {
  for (auto exp : this->seq_->GetList()) {
    exp->Traverse(env, depth);
  }
}

void AssignExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->var_->Traverse(env, depth);
  this->exp_->Traverse(env, depth);
}

void IfExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->test_->Traverse(env, depth);
  this->then_->Traverse(env, depth);
  if (this->elsee_) {
    this->elsee_->Traverse(env, depth);
  }
}

void WhileExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->test_->Traverse(env, depth);
  this->body_->Traverse(env, depth);
}

void ForExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->lo_->Traverse(env, depth);
  this->hi_->Traverse(env, depth);
  // create a new scope for the loop variable
  env->BeginScope();
  this->escape_ = false;
  env->Enter(this->var_, new esc::EscapeEntry(depth, &this->escape_));
  this->body_->Traverse(env, depth);
  env->EndScope();
}

void BreakExp::Traverse(esc::EscEnvPtr env, int depth) { return; }

void LetExp::Traverse(esc::EscEnvPtr env, int depth) {
  // A new scope
  env->BeginScope();
  if (this->decs_) {
    for (auto dec : this->decs_->GetList()) {
      dec->Traverse(env, depth);
    }
  }
  if (this->body_) {
    this->body_->Traverse(env, depth);
  }
  env->EndScope();
}

void ArrayExp::Traverse(esc::EscEnvPtr env, int depth) {
  this->size_->Traverse(env, depth);
  this->init_->Traverse(env, depth);
}

void VoidExp::Traverse(esc::EscEnvPtr env, int depth) { return; }

void FunctionDec::Traverse(esc::EscEnvPtr env, int depth) {
  for (auto fun_dec : this->functions_->GetList()) {
    env->BeginScope();
    for (auto param : fun_dec->params_->GetList()) {
      param->escape_ = false;
      env->Enter(param->name_,
                 new esc::EscapeEntry(depth + 1, &param->escape_));
    }
    fun_dec->body_->Traverse(env, depth + 1);
    env->EndScope();
  }
}

void VarDec::Traverse(esc::EscEnvPtr env, int depth) {
  if (this->init_) {
    this->init_->Traverse(env, depth);
  }
  this->escape_ = false;
  env->Enter(this->var_, new esc::EscapeEntry(depth, &this->escape_));
}

void TypeDec::Traverse(esc::EscEnvPtr env, int depth) { return; }

} // namespace absyn
