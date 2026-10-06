#include "ndlang/Interpreter.h"

#include <cstdint>
#include <cstdio>
#include <ostream>
#include <stdexcept>
#include <unordered_map>

namespace ndlang {
namespace {

struct Value {
  Type::Kind kind = Type::Void;
  int32_t i = 0; // Int
  float f = 0;   // Float
  bool b = false;
  std::vector<float> v; // Vec

  static Value ofInt(int32_t x) { Value r; r.kind = Type::Int; r.i = x; return r; }
  static Value ofFloat(float x) { Value r; r.kind = Type::Float; r.f = x; return r; }
  static Value ofBool(bool x) { Value r; r.kind = Type::Bool; r.b = x; return r; }
  static Value ofVec(std::vector<float> x) { Value r; r.kind = Type::Vec; r.v = std::move(x); return r; }
};

std::string fmtFloat(float f) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%f", static_cast<double>(f));
  return buf;
}

class Interp {
public:
  Interp(const Program &p, std::ostream &o) : out(o) {
    for (auto &f : p.funcs)
      funcs[f->name] = f.get();
  }

  int run() {
    Value r = call(*funcs.at("main"), {});
    return r.i;
  }

private:
  std::ostream &out;
  std::unordered_map<std::string, const FuncDecl *> funcs;
  using Frame = std::vector<std::unordered_map<std::string, Value>>;
  Frame *frame = nullptr;
  Value retVal;
  bool returned = false;
  int depth = 0;

  Value *find(const std::string &n) {
    for (size_t i = frame->size(); i-- > 0;) {
      auto it = (*frame)[i].find(n);
      if (it != (*frame)[i].end())
        return &it->second;
    }
    throw std::runtime_error("internal error: unbound variable " + n);
  }

  Value call(const FuncDecl &fn, std::vector<Value> args) {
    if (++depth > 5000)
      throw std::runtime_error("stack overflow: recursion too deep");
    Frame local(1);
    for (size_t i = 0; i < fn.params.size(); ++i)
      local[0][fn.params[i].name] = std::move(args[i]);
    Frame *saved = frame;
    frame = &local;
    returned = false;
    retVal = Value();
    exec(*fn.body);
    Value r = retVal;
    returned = false;
    frame = saved;
    --depth;
    return r;
  }

  void exec(const Stmt &s) {
    if (returned)
      return;
    switch (s.kind) {
    case NodeKind::BlockStmt: {
      frame->emplace_back();
      for (auto &c : static_cast<const BlockStmt &>(s).stmts) {
        exec(*c);
        if (returned)
          break;
      }
      frame->pop_back();
      break;
    }
    case NodeKind::LetStmt: {
      auto &l = static_cast<const LetStmt &>(s);
      Value v = eval(*l.init);
      frame->back()[l.name] = std::move(v);
      break;
    }
    case NodeKind::AssignStmt: {
      auto &a = static_cast<const AssignStmt &>(s);
      Value v = eval(*a.value);
      *find(a.name) = std::move(v);
      break;
    }
    case NodeKind::IfStmt: {
      auto &i = static_cast<const IfStmt &>(s);
      if (eval(*i.cond).b)
        exec(*i.thenBlock);
      else if (i.elseBranch)
        exec(*i.elseBranch);
      break;
    }
    case NodeKind::ForStmt: {
      auto &f = static_cast<const ForStmt &>(s);
      int32_t lo = eval(*f.start).i;
      int32_t hi = eval(*f.end).i; // evaluated once, like the compiled code
      frame->emplace_back();
      for (int32_t k = lo; k < hi && !returned; ++k) {
        frame->back()[f.var] = Value::ofInt(k);
        exec(*f.body);
      }
      frame->pop_back();
      break;
    }
    case NodeKind::ReturnStmt: {
      auto &r = static_cast<const ReturnStmt &>(s);
      retVal = r.value ? eval(*r.value) : Value();
      returned = true;
      break;
    }
    case NodeKind::ExprStmt:
      eval(*static_cast<const ExprStmt &>(s).expr);
      break;
    default:
      break;
    }
  }

  static int32_t wrap(int64_t x) { return static_cast<int32_t>(static_cast<uint32_t>(x)); }

  Value eval(const Expr &e) {
    switch (e.kind) {
    case NodeKind::NumberLit: {
      auto &n = static_cast<const NumberLit &>(e);
      return n.isFloat ? Value::ofFloat(static_cast<float>(n.fval)) : Value::ofInt(wrap(n.ival));
    }
    case NodeKind::BoolLit:
      return Value::ofBool(static_cast<const BoolLit &>(e).value);
    case NodeKind::VecLit: {
      std::vector<float> v;
      for (auto &el : static_cast<const VecLit &>(e).elems)
        v.push_back(eval(*el).f);
      return Value::ofVec(std::move(v));
    }
    case NodeKind::VarRef:
      return *find(static_cast<const VarRef &>(e).name);
    case NodeKind::CastExpr:
      return Value::ofFloat(static_cast<float>(eval(*static_cast<const CastExpr &>(e).operand).i));
    case NodeKind::UnaryOp: {
      auto &u = static_cast<const UnaryOp &>(e);
      Value x = eval(*u.operand);
      if (u.op == UnOp::Not)
        return Value::ofBool(!x.b);
      if (x.kind == Type::Int) return Value::ofInt(wrap(-static_cast<int64_t>(x.i)));
      if (x.kind == Type::Float) return Value::ofFloat(-x.f);
      for (auto &el : x.v) el = -el;
      return x;
    }
    case NodeKind::BinaryOp:
      return evalBinary(static_cast<const BinaryOp &>(e));
    case NodeKind::IndexExpr: {
      auto &ix = static_cast<const IndexExpr &>(e);
      Value b = eval(*ix.base);
      int32_t k = eval(*ix.index).i;
      if (k < 0 || static_cast<size_t>(k) >= b.v.size())
        throw std::runtime_error("vector index " + std::to_string(k) + " out of range");
      return Value::ofFloat(b.v[k]);
    }
    case NodeKind::CallExpr:
      return evalCall(static_cast<const CallExpr &>(e));
    default:
      throw std::runtime_error("internal error: bad expression node");
    }
  }

  static float fop(BinOp op, float a, float b) {
    switch (op) {
    case BinOp::Add: return a + b;
    case BinOp::Sub: return a - b;
    case BinOp::Mul: return a * b;
    default: return a / b;
    }
  }

  Value evalBinary(const BinaryOp &b) {
    if (b.op == BinOp::And) {
      Value l = eval(*b.lhs);
      return l.b ? Value::ofBool(eval(*b.rhs).b) : Value::ofBool(false);
    }
    if (b.op == BinOp::Or) {
      Value l = eval(*b.lhs);
      return l.b ? Value::ofBool(true) : Value::ofBool(eval(*b.rhs).b);
    }
    Value l = eval(*b.lhs), r = eval(*b.rhs);

    if (b.type.isVec()) { // elementwise, with scalar broadcast
      size_t n = static_cast<size_t>(b.type.dim);
      std::vector<float> out(n);
      for (size_t k = 0; k < n; ++k) {
        float x = l.kind == Type::Vec ? l.v[k] : l.f;
        float y = r.kind == Type::Vec ? r.v[k] : r.f;
        out[k] = fop(b.op, x, y);
      }
      return Value::ofVec(std::move(out));
    }

    if (l.kind == Type::Bool) // == / != on bool
      return Value::ofBool(b.op == BinOp::Eq ? l.b == r.b : l.b != r.b);

    if (l.kind == Type::Int) {
      int64_t x = l.i, y = r.i;
      switch (b.op) {
      case BinOp::Add: return Value::ofInt(wrap(x + y));
      case BinOp::Sub: return Value::ofInt(wrap(x - y));
      case BinOp::Mul: return Value::ofInt(wrap(x * y));
      case BinOp::Div:
        if (y == 0) throw std::runtime_error("integer division by zero");
        return Value::ofInt(wrap(x / y));
      case BinOp::Lt: return Value::ofBool(x < y);
      case BinOp::Gt: return Value::ofBool(x > y);
      case BinOp::Le: return Value::ofBool(x <= y);
      case BinOp::Ge: return Value::ofBool(x >= y);
      case BinOp::Eq: return Value::ofBool(x == y);
      default: return Value::ofBool(x != y);
      }
    }

    float x = l.f, y = r.f;
    switch (b.op) {
    case BinOp::Add: case BinOp::Sub: case BinOp::Mul: case BinOp::Div:
      return Value::ofFloat(fop(b.op, x, y));
    case BinOp::Lt: return Value::ofBool(x < y);
    case BinOp::Gt: return Value::ofBool(x > y);
    case BinOp::Le: return Value::ofBool(x <= y);
    case BinOp::Ge: return Value::ofBool(x >= y);
    case BinOp::Eq: return Value::ofBool(x == y);
    default: return Value::ofBool(x != y);
    }
  }

  Value evalCall(const CallExpr &c) {
    std::vector<Value> args;
    for (auto &a : c.args)
      args.push_back(eval(*a));

    switch (c.builtin) {
    case Builtin::Sum: {
      float s = 0.0f;
      for (float x : args[0].v) s += x;
      return Value::ofFloat(s);
    }
    case Builtin::Dot: {
      float s = 0.0f;
      for (size_t k = 0; k < args[0].v.size(); ++k) s += args[0].v[k] * args[1].v[k];
      return Value::ofFloat(s);
    }
    case Builtin::Print:
      print(args[0]);
      return Value();
    case Builtin::None:
      break;
    }
    return call(*funcs.at(c.callee), std::move(args));
  }

  void print(const Value &v) {
    switch (v.kind) {
    case Type::Int: out << v.i << "\n"; break;
    case Type::Float: out << fmtFloat(v.f) << "\n"; break;
    case Type::Bool: out << (v.b ? "true" : "false") << "\n"; break;
    case Type::Vec: {
      out << "[";
      for (size_t k = 0; k < v.v.size(); ++k)
        out << (k ? ", " : "") << fmtFloat(v.v[k]);
      out << "]\n";
      break;
    }
    default: break;
    }
  }
};

} // namespace

int interpret(const Program &prog, std::ostream &out) {
  Interp in(prog, out);
  return in.run();
}

} // namespace ndlang
