#pragma once

#include <stdexcept>
#include <string>

namespace ndlang {

struct Diag {
  int line = 0, col = 0;
  std::string msg;
};

// Thrown by the lexer-facing parser on the first syntax error.
struct CompileError : std::runtime_error {
  Diag diag;
  CompileError(int line, int col, const std::string &msg)
      : std::runtime_error(msg), diag{line, col, msg} {}
};

inline std::string formatDiag(const std::string &file, const Diag &d) {
  return file + ":" + std::to_string(d.line) + ":" + std::to_string(d.col) +
         ": error: " + d.msg;
}

} // namespace ndlang
