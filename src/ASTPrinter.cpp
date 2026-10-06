#include "ndlang/ASTPrinter.h"

#include <ostream>

namespace ndlang {
namespace {

class Printer {
public:
  explicit Printer(std::ostream &o) : os(o) {}

  void program(const Program &p) {
    line("Program");
    ++depth;
    for (auto &f : p.funcs)
      func(*f);
    --depth;
  }

private:
  std::ostream &os;
  int depth = 0;

  void line(const std::string &s) { os << std::string(depth * 2, ' ') << s << "\n"; }

  static std::string ty(const Expr &e) {
    return e.type.isError() ? "" : " : " + e.type.str();
  }

  void func(const FuncDecl &f) {
    std::string sig = "FuncDecl: " + f.name + "(";
    for (size_t i = 0; i < f.params.size(); ++i) {
      if (i)
        sig += ", ";
      sig += f.params[i].name + ": " + f.params[i].type.str();
    }
    sig += ") -> " + f.ret.str();
    line(sig);
    ++depth;
    stmt(*f.body);
    --depth;
  }

  void stmt(const Stmt &s) {
    switch (s.kind) {
    case NodeKind::BlockStmt: {
      line("Block");
      ++depth;
      for (auto &c : static_cast<const BlockStmt &>(s).stmts)
        stmt(*c);
      --depth;
      break;
    }
    case NodeKind::LetStmt: {
      auto &l = static_cast<const LetStmt &>(s);
      line("LetStmt: " + l.name + (l.varType.isError() ? "" : " : " + l.varType.str()));
      ++depth;
      expr(*l.init);
      --depth;
      break;
    }
    case NodeKind::AssignStmt: {
      auto &a = static_cast<const AssignStmt &>(s);
      line("AssignStmt: " + a.name);
      ++depth;
      expr(*a.value);
      --depth;
      break;
    }
    case NodeKind::IfStmt: {
      auto &i = static_cast<const IfStmt &>(s);
      line("IfStmt");
      ++depth;
      expr(*i.cond);
      stmt(*i.thenBlock);
      if (i.elseBranch) {
        line("Else");
        ++depth;
        stmt(*i.elseBranch);
        --depth;
      }
      --depth;
      break;
    }
    case NodeKind::ForStmt: {
      auto &f = static_cast<const ForStmt &>(s);
      line("ForStmt: " + f.var);
      ++depth;
      expr(*f.start);
      expr(*f.end);
      stmt(*f.body);
      --depth;
      break;
    }
    case NodeKind::ReturnStmt: {
      auto &r = static_cast<const ReturnStmt &>(s);
      line("ReturnStmt");
      if (r.value) {
        ++depth;
        expr(*r.value);
        --depth;
      }
      break;
    }
    case NodeKind::ExprStmt:
      line("ExprStmt");
      ++depth;
      expr(*static_cast<const ExprStmt &>(s).expr);
      --depth;
      break;
    default:
      break;
    }
  }

  void expr(const Expr &e) {
    switch (e.kind) {
    case NodeKind::NumberLit: {
      auto &n = static_cast<const NumberLit &>(e);
      line("NumberLit: " + (n.isFloat ? std::to_string(n.fval) : std::to_string(n.ival)) + ty(e));
      break;
    }
    case NodeKind::BoolLit:
      line(std::string("BoolLit: ") + (static_cast<const BoolLit &>(e).value ? "true" : "false") + ty(e));
      break;
    case NodeKind::VarRef:
      line("VarRef: " + static_cast<const VarRef &>(e).name + ty(e));
      break;
    case NodeKind::VecLit: {
      line("VecLit" + ty(e));
      ++depth;
      for (auto &c : static_cast<const VecLit &>(e).elems)
        expr(*c);
      --depth;
      break;
    }
    case NodeKind::UnaryOp: {
      auto &u = static_cast<const UnaryOp &>(e);
      line(std::string("UnaryOp: ") + (u.op == UnOp::Neg ? "-" : "!") + ty(e));
      ++depth;
      expr(*u.operand);
      --depth;
      break;
    }
    case NodeKind::BinaryOp: {
      auto &b = static_cast<const BinaryOp &>(e);
      line(std::string("BinaryOp: ") + binOpStr(b.op) + ty(e));
      ++depth;
      expr(*b.lhs);
      expr(*b.rhs);
      --depth;
      break;
    }
    case NodeKind::CallExpr: {
      auto &c = static_cast<const CallExpr &>(e);
      line("CallExpr: " + c.callee + (c.builtin != Builtin::None ? " [builtin]" : "") + ty(e));
      ++depth;
      for (auto &a : c.args)
        expr(*a);
      --depth;
      break;
    }
    case NodeKind::IndexExpr: {
      auto &i = static_cast<const IndexExpr &>(e);
      line("IndexExpr" + ty(e));
      ++depth;
      expr(*i.base);
      expr(*i.index);
      --depth;
      break;
    }
    case NodeKind::CastExpr:
      line("CastExpr (int -> float)" + ty(e));
      ++depth;
      expr(*static_cast<const CastExpr &>(e).operand);
      --depth;
      break;
    default:
      break;
    }
  }
};

} // namespace

void printAST(const Program &prog, std::ostream &os) {
  Printer(os).program(prog);
}

} // namespace ndlang
