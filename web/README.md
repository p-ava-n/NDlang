# NDLang explorer

A browser front end for `ndlangc`. It walks a program through the compiler
one stage at a time, shows each stage's input and output side by side, and
keeps every stage's output from the latest compile so you can go back and
review earlier steps.

```
 › 0 source │ ✓ 1 lexer │ ✓ 2 parser │ ✓ 3 semantic │ ✓ 4 ir gen │ ✓ 5 optimizer │ ⠋ 6 backend │ · 7 link + run
```

## Running it

The explorer needs a built `ndlangc` (see the main README) and Node.js 18 or
newer. It has no npm dependencies. In WSL, from the repository root:

```bash
node web/server.js
```

Then open <http://localhost:8090> in a Windows browser. WSL forwards
`localhost` automatically.

The server looks for the compiler at `$NDLANGC`, then `build/ndlangc`, then
`~/ndlang-build/ndlangc`. `PORT` and `HOST` change where it listens. By
default it listens on `127.0.0.1` only, because it compiles and runs whatever
program it is sent.

## What each stage shows

| # | Stage | Left window (input) | Right window (output) | `ndlangc` call |
|---|-------|---------------------|-----------------------|----------------|
| 0 | source | the editor | | |
| 1 | lexer | source | token table; hover a token to find it in the source | `--emit-tokens` |
| 2 | parser | source | AST straight from the parser, no types yet | `--emit-parse` |
| 3 | semantic | parser's AST | typed AST; implicit `int`→`float` casts are marked | `--emit-ast` |
| 4 | ir gen | typed AST | LLVM IR at `-O0` | `--emit-ir -O0` |
| 5 | optimizer | **before** (`-O0`) | **after** (`-O1` or `-O2`), rows aligned and diffed, with instruction counts per function | `--emit-ir -O0/-O1/-O2` |
| 6 | backend | optimized IR | target assembly (directives can be hidden) | `--emit-asm` |
| 7 | link + run | native executable's output | reference interpreter's output, checked for a match | `-o program`, then `--interpret` |

A stage that fails shows the source with the error lines marked and a caret
under the column. Clicking a diagnostic jumps to it in the editor. Later
stages are shown as skipped. A program that runs longer than 5 s is killed.

## Driving it

* **run ▸▸** (`Ctrl+Enter`) runs every remaining stage. The pipeline bar shows
  which stage is running, and the view follows it.
* **step ▸|** (`Ctrl+Shift+Enter`) runs one stage at a time. This is useful for
  a walkthrough.
* `←` / `→` move between stages. Clicking a stage in the pipeline bar jumps
  to it.
* `e` returns to the editor.
* The browser's Back and Forward buttons work. Every stage has its own URL
  (`#/lex`, `#/opt`, …).

## What is kept, and caching

Only the **latest compile** is kept: its source, `-O` level and every stage's
output. That is what lets you step back through its stages. It is saved in
the browser's `localStorage`, so it survives a reload. Compiling again
replaces it, and there is no history of earlier programs. If you edit the
program after compiling, the stage views still show the compiled version, and
**load into editor** in the stage header copies it back.

**run ▸▸** always goes through to execution. The server caches each compiler
stage's result by (stage, `-O` level, source), so an unchanged program
recompiles instantly; those stages are labelled `cached`. The program itself
is always built and run again, never replayed from the cache.

## Tests

```bash
node --test web/test/server.test.js
```

These cover the API, every stage of the dot-product example, a located
semantic error, path traversal, and the diff alignment. CI runs them in the
LLVM job.

## Files

| File | Purpose |
|------|---------|
| `server.js` | HTTP server: static files, `/api/info`, `/api/examples`, `POST /api/stage` |
| `public/index.html`, `style.css` | page shell and the black-and-white terminal theme |
| `public/app.js` | pipeline, routing, the editor and every stage view |
| `public/highlight.js` | syntax highlighting for NDLang, LLVM IR, assembly and AST dumps |
| `public/diff.js` | line diff and side-by-side row alignment for the optimizer view |
