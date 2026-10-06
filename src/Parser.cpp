#include "ndlang/Parser.h"

#include <algorithm>
#include <cstdlib>

namespace ndlang {
namespace {

class Parser {
public:
  explicit Parser(const std::vector<Token> &toks) : t(toks) {}

  std::unique_ptr<Program> parseProgram() {
    auto prog = std::make_unique<Program>();
    while (!at(T_EOF))
      prog->funcs.push_back(parseFunc());
    return prog;
  }

private:
  const std::vector<Token> &t;
  size_t p = 0;

  // ---- token helpers
  const Token &cur() const { return t[p]; }
  const Token &peekAt(size_t n) const { return t[std::min(p + n, t.size() - 1)]; }
  bool at(int k) const { return cur().kind == k; }
  bool accept(int k) {
    if (!at(k))
      return false;
    ++p;
    return true;
  }
  static std::string describe(const Token &tok) {
    return tok.kind == T_EOF ? "end of file" : "'" + tok.text + "'";
  }
  [[noreturn]] void fail(const std::string &msg) const {
    throw CompileError(cur().line, cur().col, msg);
  }
  const Token &expect(int k, const char *what) {
    if (!at(k))
      fail(std::string("expected ") + what + ", found " + describe(cur()));
    return t[p++];
  }

  template <class T> std::unique_ptr<T> make(const Token &at) {
    return std::make_unique<T>(at.line, at.col);
  }

  // ---- declarations
  // funcDecl ::= "func" IDENT "(" [param ("," param)*] ")" ["->" type] block
  std::unique_ptr<FuncDecl> parseFunc() {
    const Token &kw = expect(KW_FUNC, "'func'");
    auto fn = make<FuncDecl>(kw);
    fn->name = expect(IDENT, "function name").text;
    expect(LPAREN, "'('");
    if (!at(RPAREN)) {
      do {
        const Token &id = expect(IDENT, "parameter name");
        expect(COLON, "':'");
        Param prm;
        prm.name = id.text;
        prm.line = id.line;
        prm.col = id.col;
        prm.type = parseType();
        fn->params.push_back(prm);
      } while (accept(COMMA));
    }
    expect(RPAREN, "')'");
    if (accept(ARROW))
      fn->ret = parseType();
    fn->body = parseBlock();
    return fn;
  }

  // type ::= "int" | "float" | "bool" | "vec" "<" INT ">"
  Type parseType() {
    if (accept(KW_INT)) return Type::intTy();
    if (accept(KW_FLOAT)) return Type::floatTy();
    if (accept(KW_BOOL)) return Type::boolTy();
    if (accept(KW_VEC)) {
      expect(LT, "'<'");
      const Token &n = expect(INT_LIT, "vector dimension");
      long long dim = std::atoll(n.text.c_str());
      if (dim < 1 || dim > 1024)
        throw CompileError(n.line, n.col, "vector dimension must be between 1 and 1024");
      expect(GT, "'>'");
      return Type::vec(static_cast<int>(dim));
    }
    fail("expected a type, found " + describe(cur()));
  }

  // ---- statements
  // block ::= "{" stmt* "}"
  std::unique_ptr<BlockStmt> parseBlock() {
    const Token &lb = expect(LBRACE, "'{'");
    auto blk = make<BlockStmt>(lb);
    while (!at(RBRACE)) {
      if (at(T_EOF))
        fail("expected '}' before end of file");
      blk->stmts.push_back(parseStmt());
    }
    expect(RBRACE, "'}'");
    return blk;
  }

  StmtPtr parseStmt() {
    switch (cur().kind) {
    case KW_LET: return parseLet();
    case KW_RETURN: return parseReturn();
    case KW_IF: return parseIf();
    case KW_FOR: return parseFor();
    case LBRACE: return parseBlock();
    default: break;
    }
    // assignment or expression statement
    if (at(IDENT) && peekAt(1).kind == ASSIGN) {
      auto a = make<AssignStmt>(cur());
      a->name = cur().text;
      p += 2;
      a->value = parseExpr();
      expect(SEMI, "';'");
      return StmtPtr(a.release());
    }
    auto es = make<ExprStmt>(cur());
    es->expr = parseExpr();
    expect(SEMI, "';'");
    return StmtPtr(es.release());
  }

  // let ::= "let" IDENT [":" type] "=" expr ";"
  StmtPtr parseLet() {
    auto s = make<LetStmt>(expect(KW_LET, "'let'"));
    s->name = expect(IDENT, "variable name").text;
    if (accept(COLON)) {
      s->hasDeclType = true;
      s->declType = parseType();
    }
    expect(ASSIGN, "'='");
    s->init = parseExpr();
    expect(SEMI, "';'");
    return StmtPtr(s.release());
  }

  // return ::= "return" [expr] ";"
  StmtPtr parseReturn() {
    auto s = make<ReturnStmt>(expect(KW_RETURN, "'return'"));
    if (!at(SEMI))
      s->value = parseExpr();
    expect(SEMI, "';'");
    return StmtPtr(s.release());
  }

  // if ::= "if" expr block ["else" (if | block)]
  StmtPtr parseIf() {
    auto s = make<IfStmt>(expect(KW_IF, "'if'"));
    s->cond = parseExpr();
    s->thenBlock = parseBlock();
    if (accept(KW_ELSE)) {
      if (at(KW_IF))
        s->elseBranch = parseIf();
      else
        s->elseBranch = parseBlock();
    }
    return StmtPtr(s.release());
  }

  // for ::= "for" IDENT "in" expr ".." expr block
  StmtPtr parseFor() {
    auto s = make<ForStmt>(expect(KW_FOR, "'for'"));
    s->var = expect(IDENT, "loop variable").text;
    expect(KW_IN, "'in'");
    s->start = parseExpr();
    expect(DOTDOT, "'..'");
    s->end = parseExpr();
    s->body = parseBlock();
    return StmtPtr(s.release());
  }

  // ---- expressions (lowest to highest precedence)
  ExprPtr parseExpr() { return parseOr(); }

  ExprPtr binary(const Token &opTok, BinOp op, ExprPtr l, ExprPtr r) {
    auto b = make<BinaryOp>(opTok);
    b->op = op;
    b->lhs = std::move(l);
    b->rhs = std::move(r);
    return ExprPtr(b.release());
  }

  // or ::= and ("||" and)*
  ExprPtr parseOr() {
    ExprPtr l = parseAnd();
    while (at(OR)) {
      Token op = cur();
      ++p;
      l = binary(op, BinOp::Or, std::move(l), parseAnd());
    }
    return l;
  }

  // and ::= equality ("&&" equality)*
  ExprPtr parseAnd() {
    ExprPtr l = parseEquality();
    while (at(AND)) {
      Token op = cur();
      ++p;
      l = binary(op, BinOp::And, std::move(l), parseEquality());
    }
    return l;
  }

  // equality ::= relational (("==" | "!=") relational)*
  ExprPtr parseEquality() {
    ExprPtr l = parseRelational();
    while (at(EQ) || at(NE)) {
      Token op = cur();
      ++p;
      l = binary(op, op.kind == EQ ? BinOp::Eq : BinOp::Ne, std::move(l),
                 parseRelational());
    }
    return l;
  }

  // relational ::= expr_add (("<" | ">" | "<=" | ">=") expr_add)*
  ExprPtr parseRelational() {
    ExprPtr l = parseAdd();
    while (at(LT) || at(GT) || at(LE) || at(GE)) {
      Token op = cur();
      ++p;
      BinOp b = op.kind == LT ? BinOp::Lt
                : op.kind == GT ? BinOp::Gt
                : op.kind == LE ? BinOp::Le
                                : BinOp::Ge;
      l = binary(op, b, std::move(l), parseAdd());
    }
    return l;
  }

  // expr ::= term (("+" | "-") term)*
  ExprPtr parseAdd() {
    ExprPtr l = parseTerm();
    while (at(PLUS) || at(MINUS)) {
      Token op = cur();
      ++p;
      l = binary(op, op.kind == PLUS ? BinOp::Add : BinOp::Sub, std::move(l),
                 parseTerm());
    }
    return l;
  }

  // term ::= unary (("*" | "/") unary)*
  ExprPtr parseTerm() {
    ExprPtr l = parseUnary();
    while (at(STAR) || at(SLASH)) {
      Token op = cur();
      ++p;
      l = binary(op, op.kind == STAR ? BinOp::Mul : BinOp::Div, std::move(l),
                 parseUnary());
    }
    return l;
  }

  // unary ::= ("-" | "!") unary | postfix
  ExprPtr parseUnary() {
    if (at(MINUS) || at(NOT)) {
      Token op = cur();
      ++p;
      auto u = make<UnaryOp>(op);
      u->op = op.kind == MINUS ? UnOp::Neg : UnOp::Not;
      u->operand = parseUnary();
      return ExprPtr(u.release());
    }
    return parsePostfix();
  }

  // postfix ::= primary ("[" expr "]")*
  ExprPtr parsePostfix() {
    ExprPtr e = parsePrimary();
    while (at(LBRACKET)) {
      auto ix = make<IndexExpr>(cur());
      ++p;
      ix->base = std::move(e);
      ix->index = parseExpr();
      expect(RBRACKET, "']'");
      e = ExprPtr(ix.release());
    }
    return e;
  }

  // primary ::= INT | FLOAT | "true" | "false" | IDENT ["(" args ")"]
  //           | "(" expr ")" | vecLit
  ExprPtr parsePrimary() {
    const Token &tok = cur();
    switch (tok.kind) {
    case INT_LIT: {
      auto n = make<NumberLit>(tok);
      n->ival = std::atoll(tok.text.c_str());
      ++p;
      return ExprPtr(n.release());
    }
    case FLOAT_LIT: {
      auto n = make<NumberLit>(tok);
      n->isFloat = true;
      n->fval = std::atof(tok.text.c_str());
      ++p;
      return ExprPtr(n.release());
    }
    case KW_TRUE:
    case KW_FALSE: {
      auto b = make<BoolLit>(tok);
      b->value = tok.kind == KW_TRUE;
      ++p;
      return ExprPtr(b.release());
    }
    case IDENT: {
      ++p;
      if (at(LPAREN)) {
        auto c = make<CallExpr>(tok);
        c->callee = tok.text;
        ++p;
        if (!at(RPAREN)) {
          do {
            c->args.push_back(parseExpr());
          } while (accept(COMMA));
        }
        expect(RPAREN, "')'");
        return ExprPtr(c.release());
      }
      auto v = make<VarRef>(tok);
      v->name = tok.text;
      return ExprPtr(v.release());
    }
    case LPAREN: {
      ++p;
      ExprPtr e = parseExpr();
      expect(RPAREN, "')'");
      return e;
    }
    case LBRACKET:
      return parseVecLit();
    default:
      fail("expected an expression, found " + describe(tok));
    }
  }

  // vecLit ::= "[" expr ("," expr)* "]"
  ExprPtr parseVecLit() {
    auto v = make<VecLit>(expect(LBRACKET, "'['"));
    do {
      v->elems.push_back(parseExpr());
    } while (accept(COMMA));
    expect(RBRACKET, "']'");
    return ExprPtr(v.release());
  }
};

} // namespace

std::unique_ptr<Program> parse(const std::vector<Token> &tokens) {
  Parser p(tokens);
  return p.parseProgram();
}

} // namespace ndlang
