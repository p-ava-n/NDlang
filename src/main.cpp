// ndlangc - the NDLang compiler driver.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "ndlang/AST.h"
#include "ndlang/ASTPrinter.h"
#include "ndlang/Interpreter.h"
#include "ndlang/Lexer.h"
#include "ndlang/Parser.h"
#include "ndlang/Sema.h"
#ifdef NDLANG_HAVE_LLVM
#include "ndlang/CodeGen.h"
#endif

using namespace ndlang;

namespace {

const char *kUsage =
    "usage: ndlangc <input.ndlang> [options]\n"
    "\n"
    "  -o <file>        output file (default: input name without extension)\n"
    "  --emit-tokens    print the token stream and stop\n"
    "  --emit-ast       print the type-checked AST and stop\n"
    "  --emit-ir        print LLVM IR to stdout (or -o) and stop\n"
    "  --emit-obj       write an object file and stop (no linking)\n"
    "  --interpret      run the program with the reference interpreter (no LLVM)\n"
    "  -O0 | -O1 | -O2  optimisation: none | demo pipeline (default) | LLVM -O2\n"
    "  --target <triple> cross-compile to <triple> (implies --emit-obj)\n"
    "  -h, --help       show this help\n";

bool readFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

std::string stripExt(const std::string &p) {
  size_t slash = p.find_last_of("/\\");
  size_t dot = p.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    return p;
  return p.substr(0, dot);
}

int report(const std::string &file, const std::vector<Diag> &diags) {
  for (auto &d : diags)
    std::cerr << formatDiag(file, d) << "\n";
  std::cerr << diags.size() << " error(s) generated.\n";
  return 1;
}

#ifdef NDLANG_HAVE_LLVM
// Links `obj` into `exe` with the system C compiler (clang, gcc or cc).
bool linkExecutable(const std::string &obj, const std::string &exe) {
  const char *compilers[] = {"clang", "gcc", "cc"};
  for (const char *cc : compilers) {
    std::string cmd = std::string(cc) + " \"" + obj + "\" -o \"" + exe + "\"";
#ifdef _WIN32
    cmd += " >nul 2>nul";
#else
    cmd += " >/dev/null 2>&1";
#endif
    if (std::system(cmd.c_str()) == 0)
      return true;
  }
  return false;
}
#endif

} // namespace

int main(int argc, char **argv) {
  std::string input, output, target;
  bool emitTokens = false, emitAst = false, emitIr = false, emitObj = false,
       interp = false;
  int optLevel = 1;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { std::cout << kUsage; return 0; }
    else if (a == "-o" && i + 1 < argc) output = argv[++i];
    else if (a == "--target" && i + 1 < argc) { target = argv[++i]; emitObj = true; }
    else if (a == "--emit-tokens") emitTokens = true;
    else if (a == "--emit-ast") emitAst = true;
    else if (a == "--emit-ir") emitIr = true;
    else if (a == "--emit-obj") emitObj = true;
    else if (a == "--interpret") interp = true;
    else if (a == "-O0") optLevel = 0;
    else if (a == "-O1") optLevel = 1;
    else if (a == "-O2") optLevel = 2;
    else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option: " << a << "\n" << kUsage; return 2; }
    else if (input.empty()) input = a;
    else { std::cerr << "multiple input files given\n"; return 2; }
  }
  if (input.empty()) { std::cerr << kUsage; return 2; }

  std::string src;
  if (!readFile(input, src)) {
    std::cerr << "error: cannot read '" << input << "'\n";
    return 1;
  }

  // Stage 1: lexer
  std::vector<Diag> diags;
  std::vector<Token> tokens = lex(src, diags);
  if (!diags.empty())
    return report(input, diags);
  if (emitTokens) {
    for (auto &t : tokens)
      std::cout << t.line << ":" << t.col << "\t" << tokName(t.kind)
                << (t.text.empty() ? "" : "\t" + t.text) << "\n";
    return 0;
  }

  // Stage 2: parser
  std::unique_ptr<Program> prog;
  try {
    prog = parse(tokens);
  } catch (const CompileError &e) {
    return report(input, {e.diag});
  }

  // Stage 3: semantic analysis
  if (!analyze(*prog, diags))
    return report(input, diags);
  if (emitAst) {
    printAST(*prog, std::cout);
    return 0;
  }

  if (interp) {
    try {
      std::cout.flush();
      return interpret(*prog, std::cout);
    } catch (const std::exception &e) {
      std::cerr << "runtime error: " << e.what() << "\n";
      return 1;
    }
  }

#ifdef NDLANG_HAVE_LLVM
  // Stages 4-7: IR generation, optimisation, backend, link
  std::string err;
  CodeGen cg(input, target);
  if (!cg.generate(*prog, err)) { std::cerr << "error: " << err << "\n"; return 1; }
  if (!cg.optimize(optLevel, err)) { std::cerr << "error: " << err << "\n"; return 1; }

  if (emitIr) {
    if (output.empty()) {
      std::cout << cg.ir();
    } else {
      std::ofstream(output) << cg.ir();
    }
    return 0;
  }

  std::string base = output.empty() ? stripExt(input) : output;
  if (emitObj) {
    std::string obj = output.empty() ? base + ".o" : output;
    if (!cg.emitObject(obj, err)) { std::cerr << "error: " << err << "\n"; return 1; }
    return 0;
  }

  std::string obj = base + ".o";
#ifdef _WIN32
  std::string exe = output.empty() ? base + ".exe" : output;
#else
  std::string exe = base;
#endif
  if (!cg.emitObject(obj, err)) { std::cerr << "error: " << err << "\n"; return 1; }
  bool linked = linkExecutable(obj, exe);
  std::remove(obj.c_str());
  if (!linked) {
    std::cerr << "error: linking failed (need clang, gcc or cc on PATH)\n";
    return 1;
  }
  return 0;
#else
  (void)emitIr; (void)emitObj; (void)optLevel; (void)target; (void)stripExt;
  std::cerr << "error: this build of ndlangc has no LLVM backend; only "
               "--emit-tokens, --emit-ast and --interpret are available.\n"
               "Rebuild with LLVM installed (see README.md).\n";
  return 1;
#endif
}
