# Recursive descent vs. Bison/LR

The same **expression grammar** is parsed two ways:

* `src/Parser.cpp`: hand-written recursive descent for the *whole* language;
* `tools/bison_expr/expr.y`: a Bison-generated LALR(1) parser for the
  *expression subgrammar only* (numbers, `[a, b, c]` vector literals,
  `+ - * /`, unary minus, parentheses). Build target: `ndlang-bison-expr`.

```
ndlang-bison-expr "[1,2,3]*2+[4,5,6]"   ->  [6.000000, 9.000000, 12.000000]
ndlang-bison-expr "[1,2,3]+[1,2]"       ->  error: vector dimension mismatch
```

| Aspect | Recursive descent (hand-written) | Bison / LR (generated) |
|---|---|---|
| Scope in this project | full language | expression subgrammar only |
| Precedence | implicit: `parseTerm` calls `parseUnary`, so `*` binds tighter than `+` | explicit `%left '+' '-'`, `%left '*' '/'`, `%precedence UMINUS` |
| Conflict resolution | none, the grammar is written unambiguously | resolved by those declarations (see below) |
| Adding `[1,2,3]` literals | one new case in `parsePrimary` plus `parseVecLit` | edit `expr.y` (`'[' list ']'`, `list`) and regenerate the tables |
| Error messages | tailored per rule ("expected ';', found 'return'") | generic unless `%define parse.error verbose` |
| Parse tables | none, the control flow *is* the parser | generated |

## Measurements (Bison 3.8.2)

| | |
|---|---|
| Grammar rules | 12 |
| LALR(1) states | 24 |
| `YYLAST` (parse table size) | 28 |
| Shift/reduce conflicts **with** precedence declarations | **0** |
| Shift/reduce conflicts **without** them (ambiguous `expr op expr` rules) | **16** |

The 16 conflicts are the usual `expr '+' expr . '*'` ambiguities: should the
parser reduce or shift? The `%left` / `%precedence` lines remove exactly
these, which is the point of the comparison. Recursive descent bakes
precedence into the *shape of the functions*; LR makes you *declare* it.

Adding vector literals to the LR grammar added the `'[' list ']'` and `list`
rules and introduced no new conflicts. In the recursive-descent parser it was
one `case LBRACKET:` in `parsePrimary`.

On LL(1) vs LALR(1): the recursive-descent parser needs one token of lookahead
(two to tell `x = ...` from an expression statement, via `peekAt(1)`), and has
no table at all.

## Reproducing

Configure with CMake and read `<build>/expr.output` (Bison's `-v` report), or run

```bash
bison -v --report-file=expr.output tools/bison_expr/expr.y
```
