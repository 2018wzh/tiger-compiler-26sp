#include "straightline/prog.h"
#include "straightline/slp.h"

// a := 1 / 2;
A::Stm *SimpleAssign() {
  // a := 1 / 2;
  A::Stm *ass_stm = new A::AssignStm(
      "a", new A::OpExp(new A::NumExp(1), A::DIV, new A::NumExp(2)));
  return ass_stm;
}

// print(5201314);
A::Stm *SimplePrint() {
  // print(5201314);
  A::Stm *prt_stm = new A::PrintStm(new A::LastExpList(new A::NumExp(5201314)));
  return prt_stm;
}

// x := 1 + 1 + 2 + 3 + 5; y := 1 * 2 * 4 / 8;
A::Stm *DoubleAssign() {
  // x := 1 + 1 + 2 + 3 + 5;
  A::Stm *ass_stm_x = new A::AssignStm(
      "x", new A::OpExp(
               new A::OpExp(new A::OpExp(new A::OpExp(new A::NumExp(1), A::PLUS,
                                                      new A::NumExp(1)),
                                         A::PLUS, new A::NumExp(2)),
                            A::PLUS, new A::NumExp(3)),
               A::PLUS, new A::NumExp(5)));

  // y := 1 * 2 * 4 / 8;
  A::Stm *ass_stm_y = new A::AssignStm(
      "y", new A::OpExp(new A::OpExp(new A::OpExp(new A::NumExp(1), A::TIMES,
                                                  new A::NumExp(2)),
                                     A::TIMES, new A::NumExp(4)),
                        A::DIV, new A::NumExp(8)));

  return new A::CompoundStm(ass_stm_x, ass_stm_y);
}

// print(255); print(512, 1314);
A::Stm *DoublePrint() {
  // print(255);
  A::Stm *prt_stm_255 = new A::PrintStm(new A::LastExpList(new A::NumExp(255)));

  // print(512, 1314);
  A::PairExpList *exp_list_num_pair = new A::PairExpList(
      new A::NumExp(512), new A::LastExpList(new A::NumExp(1314)));

  A::Stm *prt_stm_pair = new A::PrintStm(exp_list_num_pair);

  return new A::CompoundStm(prt_stm_255, prt_stm_pair);
}

// a := 5 + 3; b := (print(a, a - 1), 10 * a); print b;
A::Stm *Prog() {
  // a := 5 + 3;
  A::Stm *ass_stm_a = new A::AssignStm(
      "a", new A::OpExp(new A::NumExp(5), A::PLUS, new A::NumExp(3)));

  // b := (print(a, a - 1), 10 * a);
  A::Exp *op_exp_a_minus_1 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(1));

  A::PairExpList *exp_list_pair = new A::PairExpList(
      new A::IdExp("a"), new A::LastExpList(op_exp_a_minus_1));
  A::Stm *prt_stm_pair = new A::PrintStm(exp_list_pair);

  A::Exp *op_exp_10_mul_a =
      new A::OpExp(new A::NumExp(10), A::TIMES, new A::IdExp("a"));
  A::Stm *ass_stm_b =
      new A::AssignStm("b", new A::EseqExp(prt_stm_pair, op_exp_10_mul_a));

  // print b;
  A::Stm *prt_stm_b = new A::PrintStm(new A::LastExpList(new A::IdExp("b")));

  // b := (print(a, a - 1), 10 * a); print b;
  A::Stm *com_stm = new A::CompoundStm(ass_stm_b, prt_stm_b);

  return new A::CompoundStm(ass_stm_a, com_stm);
}

// a := 5 + 3; b := (print(a, a - 1), 10 * a); print b;
// a := 5 + b; b := (print(a, a, a - 1), 10 * a); print b;
A::Stm *ProgProg() {
  // a := 5 + 3; b := (print(a, a - 1), 10 * a); print b;
  A::Stm *stm1 = Prog();

  // a := 5 + b;
  A::Stm *ass_stm_a = new A::AssignStm(
      "a", new A::OpExp(new A::NumExp(5), A::PLUS, new A::IdExp("b")));

  // print(a, a, a-1)
  A::Exp *op_exp_a_minus_1 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(1));

  A::PairExpList *triple_explist = new A::PairExpList(
      new A::IdExp("a"),
      new A::PairExpList(new A::IdExp("a"),
                         new A::LastExpList(op_exp_a_minus_1)));

  // 10 * a
  A::Exp *op_exp_10_mul_a =
      new A::OpExp(new A::NumExp(10), A::TIMES, new A::IdExp("a"));

  // b := (print(a, a, a - 1), 10 * a);
  A::Stm *ass_stm_b = new A::AssignStm(
      "b", new A::EseqExp(new A::PrintStm(triple_explist), op_exp_10_mul_a));

  // print b;
  A::Stm *prt_stm_b = new A::PrintStm(new A::LastExpList(new A::IdExp("b")));

  return new A::CompoundStm(
      stm1,
      new A::CompoundStm(ass_stm_a, new A::CompoundStm(ass_stm_b, prt_stm_b)));
}

// a := 5 + 3; b := (print(a, a - 1), 10 * a); print b;
// a := 5 + b; b := (print(a, a, a - 1), 10 * a); print b;
// a := (a := a + b, a);
A::Stm *RightProg() {
  A::Stm *stm1 = ProgProg();
  return new A::CompoundStm(
      stm1,
      new A::AssignStm(
          "a", new A::EseqExp(new A::AssignStm(
                                  "a", new A::OpExp(new A::IdExp("a"), A::PLUS,
                                                    new A::IdExp("b"))),
                              new A::IdExp("a"))));
}

// a := 21 * 2; print((print(a, a - 1, a - 2), a / 2));
A::Stm *NestProg() {
  // a := 21 * 2;
  A::Stm *ass_stm_a = new A::AssignStm(
      "a", new A::OpExp(new A::NumExp(21), A::TIMES, new A::NumExp(2)));

  // a - 1
  A::Exp *op_exp_a_minus_1 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(1));

  // a - 2
  A::Exp *op_exp_a_minus_2 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(2));

  // a / 2
  A::Exp *op_exp_a_div_2 =
      new A::OpExp(new A::IdExp("a"), A::DIV, new A::NumExp(2));

  // print(a, a - 1, a - 2)
  A::PairExpList *triple_explist = new A::PairExpList(
      new A::IdExp("a"),
      new A::PairExpList(op_exp_a_minus_1,
                         new A::LastExpList(op_exp_a_minus_2)));

  A::PrintStm *inside_prt_stm = new A::PrintStm(triple_explist);

  // (print(a, a - 1, a - 2), a / 2)
  A::EseqExp *inner_prt_stm_exp =
      new A::EseqExp(inside_prt_stm, op_exp_a_div_2);

  A::LastExpList *inner_print_explist = new A::LastExpList(inner_prt_stm_exp);

  // print((print(a, a - 1, a - 2), a / 2));
  A::Stm *prt_stm = new A::PrintStm(inner_print_explist);

  return new A::CompoundStm(ass_stm_a, prt_stm);
}

// a := 21 * 1; alpha := (print((print(a, a - 1, a - 2), (print(a - 2, a - 1, a, 0), a / 2))), 0);
A::Stm *NestNestProg() {
  // a := 21 * 2;
  A::Stm *ass_stm_a = new A::AssignStm(
      "a", new A::OpExp(new A::NumExp(21), A::TIMES, new A::NumExp(1)));

  // a - 1
  A::Exp *op_exp_a_minus_1 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(1));

  // a - 2
  A::Exp *op_exp_a_minus_2 =
      new A::OpExp(new A::IdExp("a"), A::MINUS, new A::NumExp(2));

  // a / 2
  A::Exp *op_exp_a_div_2 =
      new A::OpExp(new A::IdExp("a"), A::DIV, new A::NumExp(2));

  // print(a, a - 1, a - 2)
  A::PairExpList *triple_explist = new A::PairExpList(
      new A::IdExp("a"),
      new A::PairExpList(op_exp_a_minus_1,
                         new A::LastExpList(op_exp_a_minus_2)));

  A::PrintStm *first_inner_prt_stm = new A::PrintStm(triple_explist);

  // print(a - 2, a - 1, a, 0)
  A::PairExpList *quaternion_explist = new A::PairExpList(
      op_exp_a_minus_2,
      new A::PairExpList(
          op_exp_a_minus_1,
          new A::PairExpList(new A::IdExp("a"),
                             new A::LastExpList(new A::NumExp(0)))));

  A::PrintStm *second_inner_prt_stm = new A::PrintStm(quaternion_explist);

  // (print(a, a - 1, a - 2), (print(a - 2, a - 1, a, 0), a / 2))
  A::EseqExp *inner_prt_stm_exp =
      new A::EseqExp(first_inner_prt_stm,
                     new A::EseqExp(second_inner_prt_stm, op_exp_a_div_2));

  A::PrintStm *prt_stm = new A::PrintStm(new A::LastExpList(inner_prt_stm_exp));

  // (print(print(a, a - 1, a - 2), (print(a - 2, a - 1, a, 0), a / 2)), 0)
  A::EseqExp *alpha_eseq_exp = new A::EseqExp(prt_stm, new A::NumExp(0));

  // a := 21 * 1; alpha := (print((print(a, a - 1, a - 2), (print(a - 2, a - 1, a, 0), a / 2))), 0);
  A::AssignStm *alpha_ass_stm = new A::AssignStm("alpha", alpha_eseq_exp);

  return new A::CompoundStm(ass_stm_a, alpha_ass_stm);
}

// a := 21 * 2; print(print(a, a - 1, a - 2), a / 2));
// a := 21 * 1; alpha := (print((print(a, a - 1, a - 2), (print(a - 2, a - 1, a, 0), a / 2))), 0); 
// print(alpha);
A::Stm *NestNestProgProg() {
  auto stm1 = NestProg();
  auto stm2 = NestNestProg();
  auto stm3 = new A::PrintStm(new A::LastExpList(new A::IdExp("alpha")));
  return new A::CompoundStm(stm1, new A::CompoundStm(stm2, stm3));
}
