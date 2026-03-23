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

"/*" {adjust(); begin(COMMENT); comment_level_ = 1;}

<COMMENT>{
  "/*" { comment_level_++; }
  "*/" { if (--comment_level_ == 0) begin(INITIAL); }
  "\n" { adjust(); errormsg_->Newline(); }
  .    { adjust(); }
}

/* string literal: switch to STR or process directly */
"\""([^\\\"]|\\.)*"\"" {
  std::string s = matched();
  if (s.size() >= 2) s = s.substr(1, s.size() - 2); /* remove surrounding quotes */
  std::string out;
  out.reserve(s.size());
  auto isFormatting = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\f';
  };
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '\\') { out.push_back(s[i]); ++i; continue; }
    /* s[i] == '\\' */
    if (i + 1 >= s.size()) { ++i; break; }
    char c = s[i+1];
    /* Simple escapes */
    if (c == 'n') { out.push_back('\n'); i += 2; continue; }
    if (c == 't') { out.push_back('\t'); i += 2; continue; }
    if (c == '\\') { out.push_back('\\'); i += 2; continue; }
    if (c == '"') { out.push_back('"'); i += 2; continue; }

    /* control: \^c */
    if (c == '^' && i + 2 < s.size()) {
      char cc = s[i+2];
      char ctrl = static_cast<char>(cc & 0x1F);
      out.push_back(ctrl);
      i += 3;
      continue;
    }

    /* \ddd decimal ASCII (three digits) */
    if (c >= '0' && c <= '9' && i + 3 < s.size() && isdigit(static_cast<unsigned char>(s[i+2])) && isdigit(static_cast<unsigned char>(s[i+3]))) {
      int code = (s[i+1]-'0')*100 + (s[i+2]-'0')*10 + (s[i+3]-'0');
      out.push_back(static_cast<char>(code));
      i += 4;
      continue;
    }

    /* \f___f\  -> ignored */
    if (isFormatting(c)) {
      size_t j = i + 1;
      while (j < s.size() && isFormatting(s[j])) ++j;
      if (j < s.size() && s[j] == '\\') {
        i = j + 1;
        continue;
      }
    }
    /* any other condition is not allowed */
    errormsg_->Error(errormsg_->tok_pos_, "illegal token");
  }
  setMatched(out);
  adjustStr();
  return Parser::STRING;
}
[-]?{digit}+ {adjust(); return Parser::INT;}

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
