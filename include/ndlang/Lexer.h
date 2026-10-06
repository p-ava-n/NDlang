#pragma once

#include <string>
#include <vector>

#include "ndlang/Diagnostics.h"
#include "ndlang/Token.h"

namespace ndlang {

struct Token {
  int kind = T_EOF;
  std::string text;
  int line = 1, col = 1;
};

// Scans `src` with the Flex-generated scanner. The returned stream always
// ends with a T_EOF token. Unknown characters are reported in `diags` and
// skipped. Not thread-safe (Flex uses global scanner state).
std::vector<Token> lex(const std::string &src, std::vector<Diag> &diags);

} // namespace ndlang
