#pragma once

#include <vector>

#include "ndlang/AST.h"
#include "ndlang/Diagnostics.h"

namespace ndlang {

// Type-checks `prog` in place: fills in every Expr::type, resolves builtin
// calls, and inserts CastExpr nodes for int -> float widening.
// Returns true when no errors were found; otherwise errors are in `diags`.
bool analyze(Program &prog, std::vector<Diag> &diags);

} // namespace ndlang
