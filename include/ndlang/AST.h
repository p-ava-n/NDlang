#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ndlang/Type.h"

namespace ndlang {

// Order matters: Expr kinds come first, then Stmt kinds, so the range checks
// in Expr::classof / Stmt::classof stay one-liners.
enum class NodeKind {
  // expressions
  NumberLit, BoolLit, VecLit, VarRef, UnaryOp, BinaryOp, CallExpr, IndexExpr,
  CastExpr,
  // statements
  LetStmt, AssignStmt, IfStmt, ForStmt, ReturnStmt, ExprStmt, BlockStmt,
  // declarations
  FuncDecl, Program
};

struct ASTNode {
  NodeKind kind;
  int line, col;
  ASTNode(NodeKind k, int l, int c) : kind(k), line(l), col(c) {}
  virtual ~ASTNode() = default;
};

// LLVM-style RTTI: every concrete node defines classof().
template <class T> bool isa(const ASTNode *n) { return n && T::classof(n); }
template <class T> T *dyn_cast(ASTNode *n) {
  return isa<T>(n) ? static_cast<T *>(n) : nullptr;
}
template <class T> const T *dyn_cast(const ASTNode *n) {
  return isa<T>(n) ? static_cast<const T *>(n) : nullptr;
}

// ---------------------------------------------------------------- expressions

// Every expression carries a Type, filled in during semantic analysis.
struct Expr : ASTNode {
  Type type;
  Expr(NodeKind k, int l, int c) : ASTNode(k, l, c) {}
  static bool classof(const ASTNode *n) {
    return n->kind >= NodeKind::NumberLit && n->kind <= NodeKind::CastExpr;
  }
};
using ExprPtr = std::unique_ptr<Expr>;

struct NumberLit : Expr {
  bool isFloat = false;
  long long ival = 0;
  double fval = 0.0;
  NumberLit(int l, int c) : Expr(NodeKind::NumberLit, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::NumberLit; }
};

struct BoolLit : Expr {
  bool value = false;
  BoolLit(int l, int c) : Expr(NodeKind::BoolLit, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::BoolLit; }
};

struct VecLit : Expr {
  std::vector<ExprPtr> elems;
  VecLit(int l, int c) : Expr(NodeKind::VecLit, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::VecLit; }
};

struct VarRef : Expr {
  std::string name;
  VarRef(int l, int c) : Expr(NodeKind::VarRef, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::VarRef; }
};

enum class UnOp { Neg, Not };

struct UnaryOp : Expr {
  UnOp op = UnOp::Neg;
  ExprPtr operand;
  UnaryOp(int l, int c) : Expr(NodeKind::UnaryOp, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::UnaryOp; }
};

enum class BinOp { Add, Sub, Mul, Div, Lt, Gt, Le, Ge, Eq, Ne, And, Or };

inline const char *binOpStr(BinOp op) {
  switch (op) {
  case BinOp::Add: return "+";
  case BinOp::Sub: return "-";
  case BinOp::Mul: return "*";
  case BinOp::Div: return "/";
  case BinOp::Lt: return "<";
  case BinOp::Gt: return ">";
  case BinOp::Le: return "<=";
  case BinOp::Ge: return ">=";
  case BinOp::Eq: return "==";
  case BinOp::Ne: return "!=";
  case BinOp::And: return "&&";
  case BinOp::Or: return "||";
  }
  return "?";
}

struct BinaryOp : Expr {
  BinOp op = BinOp::Add;
  ExprPtr lhs, rhs;
  BinaryOp(int l, int c) : Expr(NodeKind::BinaryOp, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::BinaryOp; }
};

enum class Builtin { None, Sum, Dot, Print };

struct CallExpr : Expr {
  std::string callee;
  std::vector<ExprPtr> args;
  Builtin builtin = Builtin::None; // set by semantic analysis
  CallExpr(int l, int c) : Expr(NodeKind::CallExpr, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::CallExpr; }
};

struct IndexExpr : Expr {
  ExprPtr base, index;
  IndexExpr(int l, int c) : Expr(NodeKind::IndexExpr, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::IndexExpr; }
};

// Inserted by semantic analysis for the implicit int -> float widening.
struct CastExpr : Expr {
  ExprPtr operand;
  CastExpr(int l, int c) : Expr(NodeKind::CastExpr, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::CastExpr; }
};

// ----------------------------------------------------------------- statements

struct Stmt : ASTNode {
  Stmt(NodeKind k, int l, int c) : ASTNode(k, l, c) {}
  static bool classof(const ASTNode *n) {
    return n->kind >= NodeKind::LetStmt && n->kind <= NodeKind::BlockStmt;
  }
};
using StmtPtr = std::unique_ptr<Stmt>;

struct BlockStmt : Stmt {
  std::vector<StmtPtr> stmts;
  BlockStmt(int l, int c) : Stmt(NodeKind::BlockStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::BlockStmt; }
};

struct LetStmt : Stmt {
  std::string name;
  bool hasDeclType = false;
  Type declType;
  ExprPtr init;
  Type varType; // resolved by semantic analysis
  LetStmt(int l, int c) : Stmt(NodeKind::LetStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::LetStmt; }
};

struct AssignStmt : Stmt {
  std::string name;
  ExprPtr value;
  AssignStmt(int l, int c) : Stmt(NodeKind::AssignStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::AssignStmt; }
};

struct IfStmt : Stmt {
  ExprPtr cond;
  std::unique_ptr<BlockStmt> thenBlock;
  StmtPtr elseBranch; // BlockStmt, IfStmt (else-if) or null
  IfStmt(int l, int c) : Stmt(NodeKind::IfStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::IfStmt; }
};

// for i in start..end { ... }   (end exclusive, evaluated once)
struct ForStmt : Stmt {
  std::string var;
  ExprPtr start, end;
  std::unique_ptr<BlockStmt> body;
  ForStmt(int l, int c) : Stmt(NodeKind::ForStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::ForStmt; }
};

struct ReturnStmt : Stmt {
  ExprPtr value; // null in void functions
  ReturnStmt(int l, int c) : Stmt(NodeKind::ReturnStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::ReturnStmt; }
};

struct ExprStmt : Stmt {
  ExprPtr expr;
  ExprStmt(int l, int c) : Stmt(NodeKind::ExprStmt, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::ExprStmt; }
};

// ---------------------------------------------------------------- declarations

struct Param {
  std::string name;
  Type type;
  int line = 0, col = 0;
};

struct FuncDecl : ASTNode {
  std::string name;
  std::vector<Param> params;
  Type ret = Type::voidTy();
  std::unique_ptr<BlockStmt> body;
  FuncDecl(int l, int c) : ASTNode(NodeKind::FuncDecl, l, c) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::FuncDecl; }
};

struct Program : ASTNode {
  std::vector<std::unique_ptr<FuncDecl>> funcs;
  Program() : ASTNode(NodeKind::Program, 1, 1) {}
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::Program; }
};

} // namespace ndlang
