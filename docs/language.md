# The NDLang language

NDLang is a small, statically typed language whose one special feature is
**first-class fixed-size vectors**. Every vector's length is known at compile
time, so there is no dynamic memory, no generics and no garbage collector.

## Types

| Type      | Meaning                               | LLVM type     |
|-----------|---------------------------------------|---------------|
| `int`     | 32-bit signed integer                 | `i32`         |
| `float`   | 32-bit float                          | `float`       |
| `bool`    | truth value                           | `i1`          |
| `vec<N>`  | `N` floats, `N` fixed at compile time | `<N x float>` |

`vec<3>` and `vec<4>` are different types. `int` widens to `float` implicitly
(assignment, arguments, return values, mixed arithmetic, vector literal
elements); it **never narrows**: `let x: int = 3.5;` is an error.

`mat<R,C>` is a stretch goal and is not implemented yet.

## Programs

A program is a list of functions and must define `func main() -> int`. A
function without `-> type` returns nothing. Functions may be defined in any
order and may be mutually recursive.

```
func dot(a: vec<3>, b: vec<3>) -> float {
    return sum(a * b);
}

func main() -> int {
    let v1: vec<3> = [1.0, 2.0, 3.0];
    let v2: vec<3> = [4.0, 5.0, 6.0];
    print(dot(v1, v2));      // 32.000000
    return 0;
}
```

## Statements

```
let x: float = 1;          // type annotation optional: let x = 1.5;
x = x + 1.0;               // assignment
if x > 2.0 { ... } else if x < 0.0 { ... } else { ... }
for i in 0..10 { ... }     // i = 0 .. 9, range end evaluated once
return x;
{ ... }                    // nested block = nested scope
```

Variables are block scoped. A name may be shadowed in an inner block but not
redeclared in the same block. Write `vec<3> =` with a space: `vec<3>=` scans as
the `>=` operator.

## Operators

Highest to lowest precedence: postfix `v[i]` and calls; unary `-` `!`;
`*` `/`; `+` `-`; `<` `>` `<=` `>=`; `==` `!=`; `&&`; `||`.
`&&` and `||` short-circuit.

Vector arithmetic is elementwise:

| Expression | Result |
|------------|--------|
| `vec + vec`, `-`, `*`, `/` (same `N`) | `vec<N>` |
| `vec * scalar`, `scalar * vec`, `vec / scalar` | `vec<N>` (scalar is broadcast) |
| `-vec` | `vec<N>` |
| `v[i]` (`i` is `int`) | `float` |

Adding a `vec<3>` to a `vec<4>` is a compile-time error.

## Builtins

| Builtin | Meaning |
|---------|---------|
| `sum(v)` | sum of a vector's elements (`llvm.vector.reduce.fadd`) |
| `dot(a, b)` | dot product of two equal-length vectors. A program may define its own `dot`, which then takes precedence |
| `print(x)` | print an `int`, `float`, `bool` or `vec`, then a newline (a thin wrapper over `printf`) |

`sum` and `print` are reserved names.

## Diagnostics

Syntax errors stop at the first problem. Semantic errors are all reported:

```
file.ndlang:5:13: error: vector dimension mismatch in '+': vec<3> and vec<4>
```

Programs that must fail to compile live in `tests/errors/`.
