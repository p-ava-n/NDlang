#pragma once

#include <memory>
#include <vector>

#include "ndlang/AST.h"
#include "ndlang/Lexer.h"

namespace ndlang {

// Hand-written recursive-descent parser (one function per grammar rule, see
// docs/grammar.ebnf). `tokens` must end with T_EOF. Throws CompileError on the
// first syntax error.
std::unique_ptr<Program> parse(const std::vector<Token> &tokens);

} // namespace ndlang
