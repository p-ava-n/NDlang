#include "ndlang/Lexer.h"

// Provided by the Flex-generated scanner (src/lexer.l).
int yylex(void);
void ndl_scan_begin(const char *src);
void ndl_scan_end(void);
extern int ndl_tok_line;
extern int ndl_tok_col;
extern char *yytext;

namespace ndlang {

std::vector<Token> lex(const std::string &src, std::vector<Diag> &diags) {
  std::vector<Token> out;
  ndl_scan_begin(src.c_str());
  for (;;) {
    int k = yylex();
    if (k == T_EOF) {
      Token eof;
      eof.kind = T_EOF;
      eof.line = ndl_tok_line;
      eof.col = ndl_tok_col;
      out.push_back(eof);
      break;
    }
    if (k == T_ERROR) {
      diags.push_back({ndl_tok_line, ndl_tok_col,
                       std::string("unexpected character '") + yytext + "'"});
      continue;
    }
    Token t;
    t.kind = k;
    t.text = yytext;
    t.line = ndl_tok_line;
    t.col = ndl_tok_col;
    out.push_back(t);
  }
  ndl_scan_end();
  return out;
}

} // namespace ndlang
