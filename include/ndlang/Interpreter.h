#pragma once

#include <iosfwd>

#include "ndlang/AST.h"

namespace ndlang {

// Reference tree-walking evaluator over the *type-checked* AST. It exists so
// the front end can be tested without LLVM and so LLVM-compiled output has an
// independent oracle. print() output goes to `out`; the return value is main's
// exit code. Throws std::runtime_error on runtime errors (e.g. int divide by
// zero, runaway recursion).
int interpret(const Program &prog, std::ostream &out);

} // namespace ndlang
