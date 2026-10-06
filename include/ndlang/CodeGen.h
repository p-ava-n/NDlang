#pragma once

#include <memory>
#include <string>

#include "ndlang/AST.h"

namespace ndlang {

// AST -> LLVM IR -> optimized IR -> object file. LLVM does the heavy lifting
// (IRBuilder, pass manager, instruction selection, object emission); this class
// only decides which LLVM calls to emit. LLVM headers stay out of this file.
class CodeGen {
public:
  // `triple` empty = host machine.
  CodeGen(const std::string &moduleName, const std::string &triple);
  ~CodeGen();

  // Lowers a type-checked program. Returns false and sets `err` on failure.
  bool generate(const Program &prog, std::string &err);

  // 0 = none, 1 = NDLang demo pipeline (mem2reg, instcombine, simplifycfg,
  // SLP vectorizer), 2 = LLVM's full -O2 pipeline (adds the loop vectorizer).
  bool optimize(int level, std::string &err);

  std::string ir() const;
  bool emitObject(const std::string &path, std::string &err);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace ndlang
