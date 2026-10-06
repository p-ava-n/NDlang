// Minimal dependency-free unit tests for the NDLang front end.
#include <iostream>
#include <sstream>
#include <string>

#include "ndlang/ASTPrinter.h"
#include "ndlang/Interpreter.h"
#include "ndlang/Lexer.h"
#include "ndlang/Parser.h"
#include "ndlang/Sema.h"

using namespace ndlang;

static int failures = 0;
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

static std::vector<int> kinds(const std::string &src) {
  std::vector<Diag> d;
  std::vector<int> out;
  for (auto &t : lex(src, d))
    out.push_back(t.kind);
  return out;
}

// Parse + analyze; returns true on success and fills `errors` otherwise.
static bool compile(const std::string &src, std::unique_ptr<Program> &prog, std::string &errors) {
  std::vector<Diag> d;
  auto toks = lex(src, d);
  try {
    prog = parse(toks);
  } catch (const CompileError &e) {
    errors = e.diag.msg;
    return false;
  }
  if (!analyze(*prog, d)) {
    for (auto &x : d)
      errors += x.msg + "\n";
    return false;
  }
  return true;
}

static std::string errorsOf(const std::string &body) {
  std::unique_ptr<Program> p;
  std::string e;
  compile("func main() -> int {\n" + body + "\nreturn 0; }", p, e);
  return e;
}

static std::string runProgram(const std::string &src) {
  std::unique_ptr<Program> p;
  std::string e;
  if (!compile(src, p, e))
    return "COMPILE ERROR: " + e;
  std::ostringstream out;
  interpret(*p, out);
  return out.str();
}

static bool has(const std::string &hay, const std::string &needle) {
  return hay.find(needle) != std::string::npos;
}

int main() {
  // --- lexer: the slide's "16 tokens from one line"
  auto k = kinds("let v1: vec<3> = [1.0, 2.0, 3.0];");
  CHECK(k.size() == 17); // 16 tokens + EOF
  CHECK(k[0] == KW_LET && k[1] == IDENT && k[2] == COLON && k[3] == KW_VEC);
  CHECK(k[4] == LT && k[5] == INT_LIT && k[6] == GT && k[7] == ASSIGN);
  CHECK(k[8] == LBRACKET && k[9] == FLOAT_LIT && k[15] == SEMI && k[16] == T_EOF);

  // ranges are INT DOTDOT INT, not a malformed float
  k = kinds("0..10");
  CHECK(k.size() == 4 && k[0] == INT_LIT && k[1] == DOTDOT && k[2] == INT_LIT);

  // comments and multi-char operators
  k = kinds("a <= b // comment\n-> != && ||");
  CHECK(k.size() == 8 && k[1] == LE && k[3] == ARROW && k[4] == NE && k[5] == AND && k[6] == OR);

  // line/column tracking and bad characters
  {
    std::vector<Diag> d;
    auto toks = lex("a\n  b @", d);
    CHECK(toks[1].line == 2 && toks[1].col == 3);
    CHECK(d.size() == 1 && d[0].line == 2 && d[0].col == 5);
  }

  // --- parser: precedence and associativity
  CHECK(runProgram("func main() -> int { print(2 + 3 * 4); return 0; }") == "14\n");
  CHECK(runProgram("func main() -> int { print((2 + 3) * 4); return 0; }") == "20\n");
  CHECK(runProgram("func main() -> int { print(10 - 3 - 2); return 0; }") == "5\n");
  CHECK(runProgram("func main() -> int { print(100 / 10 / 5); return 0; }") == "2\n");
  CHECK(runProgram("func main() -> int { print(-2 * 3); return 0; }") == "-6\n");
  CHECK(runProgram("func main() -> int { print(1 < 2 && 2 < 3 || false); return 0; }") == "true\n");

  // --- semantic analysis
  CHECK(has(errorsOf("let a: vec<3> = [1.0,2.0,3.0]; let b: vec<4> = [1.0,2.0,3.0,4.0]; let c = a + b;"),
            "dimension mismatch"));
  CHECK(has(errorsOf("let a: vec<3> = [1.0, 2.0];"), "expected vec<3>, got vec<2>"));
  CHECK(has(errorsOf("let x: int = 1.5;"), "narrow"));
  CHECK(errorsOf("let x: float = 1;").empty());                 // widening is fine
  CHECK(has(errorsOf("let x: int = 1; let x: int = 2;"), "redeclaration"));
  CHECK(errorsOf("let x: int = 1; if true { let x: int = 2; }").empty()); // shadowing in a new scope
  CHECK(has(errorsOf("let v: vec<3> = [1.0,2.0,3.0]; let w = v + 1.0;"), "invalid operands"));
  CHECK(has(errorsOf("print(nope(1));"), "undeclared function"));
  CHECK(has(errorsOf("let b: bool = 1 + true;"), "invalid operands"));

  // forward + mutual recursion work because signatures are collected first
  CHECK(runProgram("func main() -> int { print(a(3)); return 0; }\n"
                   "func a(n: int) -> int { if n == 0 { return 0; } return b(n - 1) + 1; }\n"
                   "func b(n: int) -> int { if n == 0 { return 0; } return a(n - 1) + 1; }") == "3\n");

  // --- interpreter semantics
  CHECK(runProgram("func main() -> int { let v: vec<3> = [1.0,2.0,3.0]; print(sum(v * v)); return 0; }") == "14.000000\n");
  CHECK(runProgram("func main() -> int { let v = [1.0,2.0] * 3; print(v); return 0; }") == "[3.000000, 6.000000]\n");
  CHECK(runProgram("func main() -> int { let t = 0; for i in 0..4 { t = t + i; } print(t); return 0; }") == "6\n");
  // && short-circuits: the right side must not run (it would divide by zero)
  CHECK(runProgram("func boom() -> bool { print(1 / 0); return true; }\n"
                   "func main() -> int { print(false && boom()); return 0; }") == "false\n");

  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "all unit tests passed\n";
  return 0;
}
