# Experiment 7: Flex & Bison warm-up (statement terminator change)

A small statement calculator built twice with Flex and Bison. This is kept
separate from the NDLang compiler on purpose. It is the warm-up exercise:
change the language's statement terminator and see what has to change in the
scanner and the parser.

| Version | Terminator | Files |
|---------|------------|-------|
| `calc_semi` | `;` (newlines are whitespace) | `calc_semi.l`, `calc_semi.y` |
| `calc_nl` | newline (`;` is not part of the language) | `calc_nl.l`, `calc_nl.y` |

Both versions accept the same expressions: numbers, variables `a`-`z`,
`+ - * /`, unary minus and parentheses. `x = e` assigns, and a bare
expression statement prints its value.

```
$ printf 'x = 3;\ny = x * 2 + 1;\ny;\n' | ./calc_semi
7
$ printf 'x = 3\ny = x * 2 + 1\ny\n' | ./calc_nl
7
```

## What changed

**Scanner** (`calc_semi.l` to `calc_nl.l`). A newline stops being whitespace
and becomes a token:

```diff
-[ \t\r\n]+           { /* whitespace, including newlines */ }
-.                    { return yytext[0]; /* operators, '(' ')' '=' and ';' */ }
+[ \t\r]+             { /* whitespace, newlines excluded */ }
+\n                   { return EOL; }
+.                    { return yytext[0]; /* operators, '(' ')' '=' */ }
```

**Parser** (`calc_semi.y` to `calc_nl.y`). There is a new `EOL` token, it
replaces `';'` in both statement rules, and a blank line becomes an empty
statement:

```diff
+%token EOL
 ...
-stmt    : VAR '=' expr ';'     { vars[$1] = $3; }
-        | expr ';'             { printf("%g\n", $1); }
+stmt    : VAR '=' expr EOL     { vars[$1] = $3; }
+        | expr EOL             { printf("%g\n", $1); }
+        | EOL                  { /* blank line */ }
         ;
```

The expression grammar and the precedence declarations do not change at all.
The terminator is a statement-level concern, so the change stays at that level.

## What the change does to the language

| Input | `calc_semi` | `calc_nl` |
|-------|-------------|-----------|
| several statements on one line (`x = 3; y = 4;`) | allowed | not possible |
| one statement over several lines (`x =` / `4;`) | allowed | syntax error: the newline ends the statement |
| blank lines | ignored (whitespace) | needed a new `stmt : EOL` rule |
| `x = 3;` | allowed | syntax error: `;` is an unknown character |

Neither version has shift/reduce or reduce/reduce conflicts (`bison -Wall` is
clean). Adding `EOL` adds no ambiguity, because it only ever appears at the
end of a statement.

## Building and testing

The main CMake build compiles both versions and the `exp7_terminator` ctest
runs `tests/run_exp7_tests.py` against them. For a standalone build:

```bash
cd labs/exp7_flex_bison
make            # build calc_semi and calc_nl
make demo       # run the same program through both
make diff       # show the scanner and parser diffs
```
