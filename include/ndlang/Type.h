#pragma once

#include <string>

namespace ndlang {

// A NDLang type. vec<N> and vec<M> are distinct types when N != M, which is
// what lets semantic analysis reject `vec<3> + vec<4>` at compile time.
struct Type {
  enum Kind { Error, Void, Int, Float, Bool, Vec };

  Kind kind = Error;
  int dim = 0; // only meaningful for Vec

  static Type error() { return {Error, 0}; }
  static Type voidTy() { return {Void, 0}; }
  static Type intTy() { return {Int, 0}; }
  static Type floatTy() { return {Float, 0}; }
  static Type boolTy() { return {Bool, 0}; }
  static Type vec(int n) { return {Vec, n}; }

  bool isError() const { return kind == Error; }
  bool isVoid() const { return kind == Void; }
  bool isInt() const { return kind == Int; }
  bool isFloat() const { return kind == Float; }
  bool isBool() const { return kind == Bool; }
  bool isVec() const { return kind == Vec; }
  bool isNumeric() const { return kind == Int || kind == Float; }

  bool operator==(const Type &o) const { return kind == o.kind && dim == o.dim; }
  bool operator!=(const Type &o) const { return !(*this == o); }

  std::string str() const {
    switch (kind) {
    case Int: return "int";
    case Float: return "float";
    case Bool: return "bool";
    case Void: return "void";
    case Vec: return "vec<" + std::to_string(dim) + ">";
    default: return "<error>";
    }
  }
};

// int -> float widens implicitly; nothing else converts (never narrows).
inline bool canConvert(const Type &from, const Type &to) {
  return from == to || (from.isInt() && to.isFloat());
}

} // namespace ndlang
