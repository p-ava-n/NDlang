#pragma once

#include <iosfwd>

#include "ndlang/AST.h"

namespace ndlang {

// Pretty-prints the AST as an indented tree. Expression nodes show their
// resolved type once semantic analysis has run.
void printAST(const Program &prog, std::ostream &os);

} // namespace ndlang
