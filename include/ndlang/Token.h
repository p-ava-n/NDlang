#pragma once

namespace ndlang {

// Token kinds produced by the Flex scanner (src/lexer.l).
// T_EOF must stay first: Flex returns 0 at end of input.
#define NDL_TOKENS(X)                                                        \
  X(T_EOF) X(T_ERROR)                                                        \
  X(KW_FUNC) X(KW_LET) X(KW_RETURN) X(KW_IF) X(KW_ELSE) X(KW_FOR) X(KW_IN)  \
  X(KW_TRUE) X(KW_FALSE) X(KW_INT) X(KW_FLOAT) X(KW_BOOL) X(KW_VEC)          \
  X(IDENT) X(INT_LIT) X(FLOAT_LIT)                                           \
  X(LPAREN) X(RPAREN) X(LBRACE) X(RBRACE) X(LBRACKET) X(RBRACKET)            \
  X(COMMA) X(COLON) X(SEMI) X(ARROW) X(ASSIGN) X(DOTDOT)                     \
  X(PLUS) X(MINUS) X(STAR) X(SLASH)                                          \
  X(LT) X(GT) X(LE) X(GE) X(EQ) X(NE) X(AND) X(OR) X(NOT)

enum Tok : int {
#define X(n) n,
  NDL_TOKENS(X)
#undef X
};

inline const char *tokName(int t) {
  switch (t) {
#define X(n) \
  case n:    \
    return #n;
    NDL_TOKENS(X)
#undef X
  }
  return "?";
}

} // namespace ndlang
