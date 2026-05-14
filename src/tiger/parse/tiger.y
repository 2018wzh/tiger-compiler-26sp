%filenames parser
%scanner tiger/lex/scanner.h
%baseclass-preinclude tiger/absyn/absyn.h

 /*
  * Please don't modify the lines above.
  */

%union {
  int ival;
  std::string* sval;
  sym::Symbol *sym;
  absyn::Exp *exp;
  absyn::ExpList *explist;
  absyn::Var *var;
  absyn::DecList *declist;
  absyn::Dec *dec;
  absyn::EFieldList *efieldlist;
  absyn::EField *efield;
  absyn::NameAndTyList *tydeclist;
  absyn::NameAndTy *tydec;
  absyn::FieldList *fieldlist;
  absyn::Field *field;
  absyn::FunDecList *fundeclist;
  absyn::FunDec *fundec;
  absyn::Ty *ty;
  }

%token <sym> ID
%token <sval> STRING
%token <ival> INT

%token
  COMMA COLON SEMICOLON LPAREN RPAREN LBRACK RBRACK
  LBRACE RBRACE DOT
  ARRAY IF WHILE FOR TO DO LET IN END OF
  BREAK NIL
  FUNCTION VAR TYPE

 /* token priority */
 /* TODO: Put your lab3 code here */
%right ASSIGN // :=
%left OR // |
%left AND // &
%nonassoc EQ NEQ LT LE GT GE // = <> < <= > >=
%left PLUS MINUS // + -
%left TIMES DIVIDE // * /
%left UMINUS // - (unary)
%nonassoc THEN // if-then-else
%nonassoc ELSE // if-then-else
%expect 2

%type <exp> exp expseq opexp ifexp whileexp callexp recordexp
%type <explist> actuals nonemptyactuals sequencing sequencing_exps
%type <var> lvalue one oneormore
%type <declist> decs decs_nonempty
%type <dec> decs_nonempty_s vardec
%type <efieldlist> rec rec_nonempty
%type <efield> rec_one
%type <tydeclist> tydec
%type <tydec> tydec_one
%type <fieldlist> tyfields tyfields_nonempty
%type <field> tyfield
%type <ty> ty
%type <fundeclist> fundec
%type <fundec> fundec_one

%start program

%%
program:  exp  {absyn_tree_ = std::make_unique<absyn::AbsynTree>($1);};

/* TODO: Put your lab3 code here */
exp:
    NIL  {$$ = new absyn::NilExp(scanner_.GetTokPos()); } // nil
  | INT  {$$ = new absyn::IntExp(scanner_.GetTokPos(), $1); } // 0
  | STRING  {$$ = new absyn::StringExp(scanner_.GetTokPos(), $1); } // "abc"
  | opexp {$$ = $1; }
  | callexp {$$ = $1; }
  | recordexp {$$ = $1; }
  | expseq {$$ = $1; }
  | ifexp {$$ = $1; }
  | whileexp {$$ = $1; }
  | lvalue  {$$ = new absyn::VarExp(scanner_.GetTokPos(), $1); }
  | lvalue OF exp // a[1]
    {
      auto *sub = dynamic_cast<absyn::SubscriptVar *>($1);
      if (sub == nullptr) {
        scanner_.Error(scanner_.GetTokPos(), "type error: subscript only applies to array type");
        exit(1);
      }
      auto *simple = dynamic_cast<absyn::SimpleVar *>(sub->var_);
      if (simple == nullptr) {
        scanner_.Error(scanner_.GetTokPos(), "type error: subscript only applies to array type");
        exit(1);
      }
      auto *typ = simple->sym_;
      auto *size = sub->subscript_;
      sub->var_ = nullptr;
      sub->subscript_ = nullptr;
      simple->sym_ = nullptr;
      delete simple;
      delete sub;
      $$ = new absyn::ArrayExp(scanner_.GetTokPos(), typ, size, $3);
    }
  | lvalue ASSIGN exp  {$$ = new absyn::AssignExp(scanner_.GetTokPos(), $1, $3); } // x := 1
  | MINUS exp %prec UMINUS {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::MINUS_OP, new absyn::IntExp(scanner_.GetTokPos(), 0), $2); } // -x
  | FOR ID ASSIGN exp TO exp DO exp  {$$ = new absyn::ForExp(scanner_.GetTokPos(), $2, $4, $6, $8); } // for i := 0 to 10 do print(i)
  | BREAK  {$$ = new absyn::BreakExp(scanner_.GetTokPos()); } // break
  | LET decs IN sequencing END  {$$ = new absyn::LetExp(scanner_.GetTokPos(), $2, new absyn::SeqExp(scanner_.GetTokPos(), $4)); } // let ... in ... end
  ;

expseq:
    LPAREN sequencing RPAREN // (exp; exp; exp)
    {
      if ($2->GetList().empty()) {
        delete $2;
        $$ = new absyn::VoidExp(scanner_.GetTokPos());
      } else if ($2->GetList().size() == 1) {
        auto only = $2->GetList().front();
        delete $2;
        $$ = only;
      } else {
        $$ = new absyn::SeqExp(scanner_.GetTokPos(), $2);
      }
    }
  ;

opexp:
    exp PLUS exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::PLUS_OP, $1, $3); } // x + y
  | exp MINUS exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::MINUS_OP, $1, $3); } // x - y
  | exp TIMES exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::TIMES_OP, $1, $3); } // x * y
  | exp DIVIDE exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::DIVIDE_OP, $1, $3); } // x / y
  | exp EQ exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::EQ_OP, $1, $3); } // x = y
  | exp NEQ exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::NEQ_OP, $1, $3); } // x <> y
  | exp LT exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::LT_OP, $1, $3); } // x < y
  | exp LE exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::LE_OP, $1, $3); } // x <= y
  | exp GT exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::GT_OP, $1, $3); } // x > y
  | exp GE exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::GE_OP, $1, $3); } // x >= y
  | exp AND exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::AND_OP, $1, $3); } // x & y
  | exp OR exp  {$$ = new absyn::OpExp(scanner_.GetTokPos(), absyn::OR_OP, $1, $3); } // x | y
  ;

ifexp:
    IF exp THEN exp ELSE exp  {$$ = new absyn::IfExp(scanner_.GetTokPos(), $2, $4, $6); } // if x then y else z
  | IF exp THEN exp %prec THEN  {$$ = new absyn::IfExp(scanner_.GetTokPos(), $2, $4, nullptr); } // if x then y
  ;

whileexp:
    WHILE exp DO exp  {$$ = new absyn::WhileExp(scanner_.GetTokPos(), $2, $4); } // while x do y
  ;

callexp:
    ID LPAREN actuals RPAREN  {$$ = new absyn::CallExp(scanner_.GetTokPos(), $1, $3); } // f(x, y, z)
  ;

recordexp:
    ID LBRACE rec RBRACE  {$$ = new absyn::RecordExp(scanner_.GetTokPos(), $1, $3); } // {x=1, y=2}
  ;

actuals:
    nonemptyactuals  {$$ = $1; }
  | /* empty */  {$$ = new absyn::ExpList(); } // f()
  ;

nonemptyactuals:
    exp COMMA nonemptyactuals  {$$ = $3->Prepend($1); } // f(x, y, z)
  | exp  {$$ = new absyn::ExpList($1); } // f(x)
  ;

sequencing:
    sequencing_exps  {$$ = $1; } // exp; exp; exp
  | /* empty */  {$$ = new absyn::ExpList(); }
  ;

sequencing_exps:
    exp SEMICOLON sequencing_exps  {$$ = $3->Prepend($1); } // exp; exp; exp
  | exp  {$$ = new absyn::ExpList($1); } // exp
  ;

lvalue:
    one  {$$ = $1; }
  | oneormore {$$ = $1; }
  ;

one:
    ID  {$$ = new absyn::SimpleVar(scanner_.GetTokPos(), $1); } // x
  ;

oneormore:
    lvalue DOT ID  {$$ = new absyn::FieldVar(scanner_.GetTokPos(), $1, $3); } // x.y
  | lvalue LBRACK exp RBRACK  {$$ = new absyn::SubscriptVar(scanner_.GetTokPos(), $1, $3); } // x[1]
  ;

decs:
    decs_nonempty {$$ = $1; }
  | /* empty */  {$$ = new absyn::DecList(); }
  ;

decs_nonempty:
    decs_nonempty_s decs_nonempty  {$$ = $2->Prepend($1); } // dec; dec; dec
  | decs_nonempty_s  {$$ = new absyn::DecList($1); } // dec
  ;

decs_nonempty_s:
    tydec  {$$ = new absyn::TypeDec(scanner_.GetTokPos(), $1); } // type t = ...
  | vardec  { $$ = $1; } // var x := ...
  | fundec  {$$ = new absyn::FunctionDec(scanner_.GetTokPos(), $1); } // function f(...) = ...
  ;

vardec:
    VAR ID ASSIGN exp  {$$ = new absyn::VarDec(scanner_.GetTokPos(), $2, nullptr, $4); } // var x := 1
  | VAR ID COLON ID ASSIGN exp  {$$ = new absyn::VarDec(scanner_.GetTokPos(), $2, $4, $6); } // var x: int := 1
  ;

rec:
    rec_nonempty  {$$ = $1; }
  | /* empty */  {$$ = new absyn::EFieldList(); }
  ;

rec_nonempty:
    rec_one COMMA rec_nonempty  {$$ = $3->Prepend($1); } // {x=1, y=2}
  | rec_one  {$$ = new absyn::EFieldList($1); } 
  ;

rec_one:
    ID EQ exp  {$$ = new absyn::EField($1, $3); } // x = 1
  ;

tydec:
    tydec_one tydec  {$$ = $2->Prepend($1); } // type t = ...; type u = ...; type v = ...
  | tydec_one  {$$ = new absyn::NameAndTyList($1); }
  ;

tydec_one:
    TYPE ID EQ ty  {$$ = new absyn::NameAndTy($2, $4); } // type t = int;
  ;

tyfields:
    tyfields_nonempty  {$$ = $1; }
  | /* empty */  {$$ = new absyn::FieldList(); }
  ;

tyfields_nonempty:
    tyfield COMMA tyfields_nonempty  {$$ = $3->Prepend($1); } // {x: int, y: string}
  | tyfield  {$$ = new absyn::FieldList($1); }
  ;

tyfield:
  ID COLON ID  {$$ = new absyn::Field(scanner_.GetTokPos(), $1, $3); } // x: int
  ;

ty:
    ID  {$$ = new absyn::NameTy(scanner_.GetTokPos(), $1); }
  | LBRACE tyfields RBRACE  {$$ = new absyn::RecordTy(scanner_.GetTokPos(), $2); } // {x: int, y: string}
  | ARRAY OF ID  {$$ = new absyn::ArrayTy(scanner_.GetTokPos(), $3); } // array of int
  ;

fundec:
    fundec_one fundec  {$$ = $2->Prepend($1); } // function f(...) = ...; function g(...) = ...; function h(...) = ...
  | fundec_one  {$$ = new absyn::FunDecList($1); }
  ;

fundec_one:
    FUNCTION ID LPAREN tyfields RPAREN COLON ID EQ exp  {$$ = new absyn::FunDec(scanner_.GetTokPos(), $2, $4, $7, $9); } // function f(x: int, y: string): int = ...
  | FUNCTION ID LPAREN tyfields RPAREN EQ exp  {$$ = new absyn::FunDec(scanner_.GetTokPos(), $2, $4, nullptr, $7); } // function f(x: int, y: string) = ...
  ;
