%filenames scanner

 /*
  * Please don't modify the lines above.
  */

 /* You can add lex definitions here. */
digit [0-9]
letter [a-zA-Z]

%x COMMENT STR IGNORE

%%

 /*
  * Below is examples, which you can wipe out
  * and write regular expressions and actions of your own.
  *
  * All the tokens:
  *   Parser::ID
  *   Parser::STRING
  *   Parser::INT
  *   Parser::COMMA
  *   Parser::COLON
  *   Parser::SEMICOLON
  *   Parser::LPAREN
  *   Parser::RPAREN
  *   Parser::LBRACK
  *   Parser::RBRACK
  *   Parser::LBRACE
  *   Parser::RBRACE
  *   Parser::DOT
  *   Parser::PLUS
  *   Parser::MINUS
  *   Parser::TIMES
  *   Parser::DIVIDE
  *   Parser::EQ
  *   Parser::NEQ
  *   Parser::LT
  *   Parser::LE
  *   Parser::GT
  *   Parser::GE
  *   Parser::AND
  *   Parser::OR
  *   Parser::ASSIGN
  *   Parser::ARRAY
  *   Parser::IF
  *   Parser::THEN
  *   Parser::ELSE
  *   Parser::WHILE
  *   Parser::FOR
  *   Parser::TO
  *   Parser::DO
  *   Parser::LET
  *   Parser::IN
  *   Parser::END
  *   Parser::OF
  *   Parser::BREAK
  *   Parser::NIL
  *   Parser::FUNCTION
  *   Parser::VAR
  *   Parser::TYPE
  */

 /* reserved words */
"array" {adjust(); return Parser::ARRAY;}

 /* TODO: Put your lab2 code here */

"if" {adjust(); return Parser::IF;}
"then" {adjust(); return Parser::THEN;}
"else" {adjust(); return Parser::ELSE;}
"while" {adjust(); return Parser::WHILE;}
"for" {adjust(); return Parser::FOR;}
"to" {adjust(); return Parser::TO;}
"do" {adjust(); return Parser::DO;}
"let" {adjust(); return Parser::LET;}
"in" {adjust(); return Parser::IN;}
"end" {adjust(); return Parser::END;}
"of" {adjust(); return Parser::OF;}
"break" {adjust(); return Parser::BREAK;}
"nil" {adjust(); return Parser::NIL;}
"function" {adjust(); return Parser::FUNCTION;}
"var" {adjust(); return Parser::VAR;}
"type" {adjust(); return Parser::TYPE;}

{letter}({letter}|{digit}|[_])* {adjust(); return Parser::ID;}

"/*" {adjust(); begin(StartCondition_::COMMENT); comment_level_ = 1;}

<COMMENT>{
  "/*" { adjust(); comment_level_++; }
  "*/" { adjust(); if (--comment_level_ == 0) begin(StartCondition_::INITIAL); }
  "\n" { adjust(); errormsg_->Newline(); }
  .    { adjust(); }
}

"\"" { adjust(); string_buf_.clear(); begin(StartCondition_::STR); }

<STR>{
  "\"" {
    adjustStr();
    begin(StartCondition_::INITIAL);
    setMatched(string_buf_);
    return Parser::STRING;
  }
  "\\n" { adjustStr(); string_buf_ += '\n'; }
  "\\t" { adjustStr(); string_buf_ += '\t'; }
  "\\\"" { adjustStr(); string_buf_ += '"'; }
  "\\\\" { adjustStr(); string_buf_ += '\\'; }
  \\[0-9][0-9][0-9] {
    adjustStr();
    std::string s = matched();
    int val = (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    string_buf_ += static_cast<char>(val);
  }
  "\\^"[a-zA-Z\[\\\]\^_@] {
    adjustStr();
    std::string s = matched();
    char c = s[2];
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    string_buf_ += static_cast<char>(c - '@');
  }
  \\[ \t\n\f]+\\ {
    adjustStr();
    std::string s = matched();
    for (char c : s) {
      if (c == '\n') errormsg_->Newline();
    }
  }
  \n {
    adjustStr();
    errormsg_->Newline();
    string_buf_ += '\n';
  }
  . {
    adjustStr();
    string_buf_ += matched();
  }
}

{digit}+ {adjust(); return Parser::INT;}

"," {adjust(); return Parser::COMMA;}
":" {adjust(); return Parser::COLON;}
";" {adjust(); return Parser::SEMICOLON;}
"(" {adjust(); return Parser::LPAREN;}
")" {adjust(); return Parser::RPAREN;}
"[" {adjust(); return Parser::LBRACK;}
"]" {adjust(); return Parser::RBRACK;}
"{" {adjust(); return Parser::LBRACE;}
"}" {adjust(); return Parser::RBRACE;}
"." {adjust(); return Parser::DOT;}
"+" {adjust(); return Parser::PLUS;}
"-" {adjust(); return Parser::MINUS;}
"*" {adjust(); return Parser::TIMES;}
"/" {adjust(); return Parser::DIVIDE;}
"=" {adjust(); return Parser::EQ;}
"<>" {adjust(); return Parser::NEQ;}
"<" {adjust(); return Parser::LT;}
"<=" {adjust(); return Parser::LE;}
">" {adjust(); return Parser::GT;}
">=" {adjust(); return Parser::GE;}
"&" {adjust(); return Parser::AND;}
"|" {adjust(); return Parser::OR;}
":=" {adjust(); return Parser::ASSIGN;}
 /*
  * skip white space chars.
  * space, tabs and LF
  */
[ \t]+ {adjust();}
\n {adjust(); errormsg_->Newline();}

 /* illegal input */
. {adjust(); errormsg_->Error(errormsg_->tok_pos_, "illegal token");}
