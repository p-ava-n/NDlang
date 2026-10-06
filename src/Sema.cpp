#include "ndlang/Sema.h"

#include <unordered_map>

namespace ndlang {
namespace {

class Sema {
public:
  explicit Sema(std::vector<Diag> &d) : diags(d) {}

  void run(Program &prog) {
    // Pass 1: collect every signature so functions can call each other
    // regardless of definition order (forward and mutual recursion).
    for (auto &fn : prog.funcs)
      collectSignature(*fn);

    auto mainIt = funcs.find("main");
    if (mainIt == funcs.end())
      error(1, 1, "program has no 'main' function");
    else if (!mainIt->second.params.empty() || !mainIt->second.ret.isInt())
      error(mainIt->second.line, mainIt->second.col,
            "'main' must be declared as 'func main() -> int'");

    // Pass 2: check each body.
    for (auto &fn : prog.funcs)
      checkFunc(*fn);
  }

private:
  struct Sig {
    std::vector<Type> params;
    Type ret;
    int line = 0, col = 0;
  };

  std::vector<Diag> &diags;
  std::unordered_map<std::string, Sig> funcs;
  std::vector<std::unordered_map<std::string, Type>> scopes; // scoped symbol table
  Type curRet = Type::voidTy();

  void error(int line, int col, const std::string &msg) {
    diags.push_back({line, col, msg});
  }
  void error(const ASTNode &n, const std::string &msg) { error(n.line, n.col, msg); }

  // ---- scopes
  void pushScope() { scopes.emplace_back(); }
  void popScope() { scopes.pop_back(); }
  bool declare(const std::string &name, Type ty, int line, int col) {
    if (scopes.back().count(name)) {
      error(line, col, "redeclaration of '" + name + "' in the same scope");
      return false;
    }
    scopes.back()[name] = ty;
    return true;
  }
  bool lookup(const std::string &name, Type &out) const {
    for (size_t i = scopes.size(); i-- > 0;) {
      auto it = scopes[i].find(name);
      if (it != scopes[i].end()) {
        out = it->second;
        return true;
      }
    }
    return false;
  }

  // sum and print are reserved. dot is a convenience builtin that a program
  // may override by defining its own `dot` (as the slide example does).
  static bool isReservedName(const std::string &n) { return n == "sum" || n == "print"; }
  static bool isBuiltinName(const std::string &n) { return isReservedName(n) || n == "dot"; }

  void collectSignature(const FuncDecl &fn) {
    if (isReservedName(fn.name)) {
      error(fn, "'" + fn.name + "' is a builtin and cannot be redefined");
      return;
    }
    if (funcs.count(fn.name)) {
      error(fn, "redefinition of function '" + fn.name + "'");
      return;
    }
    Sig s;
    s.ret = fn.ret;
    s.line = fn.line;
    s.col = fn.col;
    for (auto &p : fn.params)
      s.params.push_back(p.type);
    funcs[fn.name] = s;
  }

  // ---- widening
  // Wraps `e` in a CastExpr when an int must become a float.
  void widen(ExprPtr &e, const Type &target) {
    if (e->type.isInt() && target.isFloat()) {
      auto c = std::make_unique<CastExpr>(e->line, e->col);
      c->type = Type::floatTy();
      c->operand = std::move(e);
      e = std::move(c);
    }
  }

  // Coerces `e` to `target`, reporting `what` on failure.
  void coerce(ExprPtr &e, const Type &target, const std::string &what) {
    if (e->type.isError() || target.isError())
      return;
    if (!canConvert(e->type, target)) {
      std::string hint = (e->type.isFloat() && target.isInt())
                             ? " (float -> int would narrow; conversions never narrow)"
                             : "";
      error(*e, what + ": expected " + target.str() + ", got " + e->type.str() + hint);
      return;
    }
    widen(e, target);
  }

  // ---- declarations
  void checkFunc(FuncDecl &fn) {
    curRet = fn.ret;
    pushScope();
    for (auto &p : fn.params)
      declare(p.name, p.type, p.line, p.col);
    checkBlock(*fn.body, /*ownScope=*/false);
    popScope();
    if (!fn.ret.isVoid() && !alwaysReturns(*fn.body))
      error(fn, "function '" + fn.name + "' may reach its end without returning a value of type " +
                    fn.ret.str());
  }

  static bool alwaysReturns(const Stmt &s) {
    if (isa<ReturnStmt>(&s))
      return true;
    if (auto *b = dyn_cast<BlockStmt>(&s)) {
      for (auto &st : b->stmts)
        if (alwaysReturns(*st))
          return true;
      return false;
    }
    if (auto *i = dyn_cast<IfStmt>(&s))
      return i->elseBranch && alwaysReturns(*i->thenBlock) && alwaysReturns(*i->elseBranch);
    return false;
  }

  // ---- statements
  void checkBlock(BlockStmt &b, bool ownScope = true) {
    if (ownScope)
      pushScope();
    for (auto &s : b.stmts)
      checkStmt(*s);
    if (ownScope)
      popScope();
  }

  void checkStmt(Stmt &s) {
    switch (s.kind) {
    case NodeKind::BlockStmt:
      checkBlock(static_cast<BlockStmt &>(s));
      break;
    case NodeKind::LetStmt: {
      auto &l = static_cast<LetStmt &>(s);
      checkExpr(l.init);
      Type ty = l.init->type;
      if (ty.isVoid()) {
        error(*l.init, "cannot initialise '" + l.name + "' with a void value");
        ty = Type::error();
      }
      if (l.hasDeclType) {
        coerce(l.init, l.declType, "initialiser of '" + l.name + "'");
        ty = l.declType;
      }
      l.varType = ty;
      declare(l.name, ty, l.line, l.col);
      break;
    }
    case NodeKind::AssignStmt: {
      auto &a = static_cast<AssignStmt &>(s);
      checkExpr(a.value);
      Type vt;
      if (!lookup(a.name, vt)) {
        error(a, "assignment to undeclared variable '" + a.name + "'");
        break;
      }
      coerce(a.value, vt, "assignment to '" + a.name + "'");
      break;
    }
    case NodeKind::IfStmt: {
      auto &i = static_cast<IfStmt &>(s);
      checkCondition(i.cond, "'if' condition");
      checkBlock(*i.thenBlock);
      if (i.elseBranch)
        checkStmt(*i.elseBranch);
      break;
    }
    case NodeKind::ForStmt: {
      auto &f = static_cast<ForStmt &>(s);
      checkExpr(f.start);
      checkExpr(f.end);
      if (!f.start->type.isError() && !f.start->type.isInt())
        error(*f.start, "'for' range start must be int, got " + f.start->type.str());
      if (!f.end->type.isError() && !f.end->type.isInt())
        error(*f.end, "'for' range end must be int, got " + f.end->type.str());
      pushScope();
      declare(f.var, Type::intTy(), f.line, f.col);
      checkBlock(*f.body);
      popScope();
      break;
    }
    case NodeKind::ReturnStmt: {
      auto &r = static_cast<ReturnStmt &>(s);
      if (!r.value) {
        if (!curRet.isVoid())
          error(r, "missing return value (function returns " + curRet.str() + ")");
        break;
      }
      checkExpr(r.value);
      if (curRet.isVoid())
        error(r, "void function must not return a value");
      else
        coerce(r.value, curRet, "return value");
      break;
    }
    case NodeKind::ExprStmt:
      checkExpr(static_cast<ExprStmt &>(s).expr);
      break;
    default:
      break;
    }
  }

  void checkCondition(ExprPtr &e, const std::string &what) {
    checkExpr(e);
    if (!e->type.isError() && !e->type.isBool())
      error(*e, what + " must be bool, got " + e->type.str());
  }

  // ---- expressions
  void checkExpr(ExprPtr &e) {
    switch (e->kind) {
    case NodeKind::NumberLit: {
      auto &n = static_cast<NumberLit &>(*e);
      n.type = n.isFloat ? Type::floatTy() : Type::intTy();
      if (!n.isFloat && n.ival > 2147483647LL)
        error(n, "integer literal does not fit in 32 bits");
      break;
    }
    case NodeKind::BoolLit:
      e->type = Type::boolTy();
      break;
    case NodeKind::VecLit:
      checkVecLit(static_cast<VecLit &>(*e));
      break;
    case NodeKind::VarRef: {
      auto &v = static_cast<VarRef &>(*e);
      Type t;
      if (lookup(v.name, t))
        v.type = t;
      else {
        error(v, "use of undeclared variable '" + v.name + "'");
        v.type = Type::error();
      }
      break;
    }
    case NodeKind::UnaryOp:
      checkUnary(static_cast<UnaryOp &>(*e));
      break;
    case NodeKind::BinaryOp:
      checkBinary(static_cast<BinaryOp &>(*e));
      break;
    case NodeKind::CallExpr:
      checkCall(static_cast<CallExpr &>(*e));
      break;
    case NodeKind::IndexExpr:
      checkIndex(static_cast<IndexExpr &>(*e));
      break;
    default:
      break;
    }
  }

  void checkVecLit(VecLit &v) {
    bool ok = true;
    for (auto &el : v.elems) {
      checkExpr(el);
      if (el->type.isError()) {
        ok = false;
      } else if (!el->type.isNumeric()) {
        error(*el, "vector elements must be int or float, got " + el->type.str());
        ok = false;
      } else {
        widen(el, Type::floatTy());
      }
    }
    v.type = ok ? Type::vec(static_cast<int>(v.elems.size())) : Type::error();
  }

  void checkUnary(UnaryOp &u) {
    checkExpr(u.operand);
    Type t = u.operand->type;
    if (t.isError()) {
      u.type = Type::error();
      return;
    }
    if (u.op == UnOp::Neg) {
      if (t.isNumeric() || t.isVec()) {
        u.type = t;
        return;
      }
      error(u, "cannot negate a value of type " + t.str());
    } else {
      if (t.isBool()) {
        u.type = t;
        return;
      }
      error(u, "'!' requires bool, got " + t.str());
    }
    u.type = Type::error();
  }

  void checkBinary(BinaryOp &b) {
    checkExpr(b.lhs);
    checkExpr(b.rhs);
    Type l = b.lhs->type, r = b.rhs->type;
    b.type = Type::error();
    if (l.isError() || r.isError())
      return;

    auto bad = [&]() {
      error(b, std::string("invalid operands to '") + binOpStr(b.op) + "': " +
                   l.str() + " and " + r.str());
    };

    switch (b.op) {
    case BinOp::And:
    case BinOp::Or:
      if (l.isBool() && r.isBool())
        b.type = Type::boolTy();
      else
        bad();
      return;

    case BinOp::Eq:
    case BinOp::Ne:
      if (l.isBool() && r.isBool()) {
        b.type = Type::boolTy();
      } else if (l.isNumeric() && r.isNumeric()) {
        unifyNumeric(b);
        b.type = Type::boolTy();
      } else {
        bad();
      }
      return;

    case BinOp::Lt:
    case BinOp::Gt:
    case BinOp::Le:
    case BinOp::Ge:
      if (l.isNumeric() && r.isNumeric()) {
        unifyNumeric(b);
        b.type = Type::boolTy();
      } else {
        bad();
      }
      return;

    default: // + - * /
      break;
    }

    if (l.isNumeric() && r.isNumeric()) {
      b.type = unifyNumeric(b);
    } else if (l.isVec() && r.isVec()) {
      if (l.dim != r.dim)
        error(b, std::string("vector dimension mismatch in '") + binOpStr(b.op) +
                     "': " + l.str() + " and " + r.str());
      else
        b.type = l;
    } else if (l.isVec() && r.isNumeric() &&
               (b.op == BinOp::Mul || b.op == BinOp::Div)) {
      widen(b.rhs, Type::floatTy()); // scalar broadcast
      b.type = l;
    } else if (l.isNumeric() && r.isVec() && b.op == BinOp::Mul) {
      widen(b.lhs, Type::floatTy());
      b.type = r;
    } else {
      bad();
    }
  }

  // Widens the int side of a mixed int/float pair; returns the common type.
  Type unifyNumeric(BinaryOp &b) {
    if (b.lhs->type.isFloat() || b.rhs->type.isFloat()) {
      widen(b.lhs, Type::floatTy());
      widen(b.rhs, Type::floatTy());
      return Type::floatTy();
    }
    return Type::intTy();
  }

  void checkIndex(IndexExpr &ix) {
    checkExpr(ix.base);
    checkExpr(ix.index);
    ix.type = Type::error();
    Type bt = ix.base->type, it = ix.index->type;
    if (bt.isError() || it.isError())
      return;
    if (!bt.isVec()) {
      error(ix, "cannot index a value of type " + bt.str());
      return;
    }
    if (!it.isInt()) {
      error(*ix.index, "vector index must be int, got " + it.str());
      return;
    }
    if (auto *n = dyn_cast<NumberLit>(ix.index.get()))
      if (n->ival >= bt.dim)
        error(ix, "index " + std::to_string(n->ival) + " is out of range for " + bt.str());
    ix.type = Type::floatTy();
  }

  void checkCall(CallExpr &c) {
    for (auto &a : c.args)
      checkExpr(a);
    c.type = Type::error();

    bool argsOk = true;
    for (auto &a : c.args)
      if (a->type.isError())
        argsOk = false;

    auto it = funcs.find(c.callee);
    if (it == funcs.end() && isBuiltinName(c.callee)) {
      checkBuiltin(c, argsOk);
      return;
    }
    if (it == funcs.end()) {
      error(c, "call to undeclared function '" + c.callee + "'");
      return;
    }
    const Sig &sig = it->second;
    c.type = sig.ret;
    if (c.args.size() != sig.params.size()) {
      error(c, "function '" + c.callee + "' expects " + std::to_string(sig.params.size()) +
                   " argument(s), got " + std::to_string(c.args.size()));
      return;
    }
    for (size_t i = 0; i < c.args.size(); ++i)
      coerce(c.args[i], sig.params[i],
             "argument " + std::to_string(i + 1) + " of '" + c.callee + "'");
  }

  void checkBuiltin(CallExpr &c, bool argsOk) {
    auto arity = [&](size_t n) {
      if (c.args.size() == n)
        return true;
      error(c, "builtin '" + c.callee + "' expects " + std::to_string(n) +
                   " argument(s), got " + std::to_string(c.args.size()));
      return false;
    };

    if (c.callee == "sum") {
      c.builtin = Builtin::Sum;
      if (!arity(1) || !argsOk)
        return;
      if (!c.args[0]->type.isVec()) {
        error(*c.args[0], "sum() expects a vec<N>, got " + c.args[0]->type.str());
        return;
      }
      c.type = Type::floatTy();
    } else if (c.callee == "dot") {
      c.builtin = Builtin::Dot;
      if (!arity(2) || !argsOk)
        return;
      const Type &a = c.args[0]->type, &b = c.args[1]->type;
      if (!a.isVec() || !b.isVec()) {
        error(c, "dot() expects two vectors, got " + a.str() + " and " + b.str());
        return;
      }
      if (a.dim != b.dim) {
        error(c, "dot() vector dimension mismatch: " + a.str() + " and " + b.str());
        return;
      }
      c.type = Type::floatTy();
    } else { // print
      c.builtin = Builtin::Print;
      if (!arity(1) || !argsOk)
        return;
      if (c.args[0]->type.isVoid()) {
        error(*c.args[0], "cannot print a void value");
        return;
      }
      c.type = Type::voidTy();
    }
  }
};

} // namespace

bool analyze(Program &prog, std::vector<Diag> &diags) {
  size_t before = diags.size();
  Sema s(diags);
  s.run(prog);
  return diags.size() == before;
}

} // namespace ndlang
