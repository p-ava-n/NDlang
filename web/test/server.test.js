// Tests for the explorer server and the diff module.
//
//   node --test web/test
//
// The HTTP tests need a built ndlangc (found the same way the server finds
// it: $NDLANGC, build/ndlangc or ~/ndlang-build/ndlangc) and are skipped
// without one.

'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const http = require('http');
const path = require('path');

const { handle, parseDiagnostics } = require('../server.js');

const ROOT = path.resolve(__dirname, '..', '..');
const read = (p) => fs.readFileSync(path.join(ROOT, p), 'utf8');

test('parseDiagnostics reads ndlangc error lines', () => {
  const d = parseDiagnostics("program.ndlang:5:13: error: vector dimension mismatch in '+': vec<3> and vec<4>\n1 error(s) generated.\n");
  assert.deepEqual(d, [{ line: 5, col: 13, message: "vector dimension mismatch in '+': vec<3> and vec<4>" }]);
});

test('alignRows pairs deletions with insertions', async () => {
  const { diffLines, alignRows } = await import('../public/diff.js');
  const rows = alignRows(diffLines(['a', 'x1', 'x2', 'b'], ['a', 'y1', 'b', 'c']));
  assert.deepEqual(rows.map((r) => r.kind), ['eq', 'chg', 'del', 'eq', 'add']);
  assert.equal(rows[1].l, 'x1');
  assert.equal(rows[1].r, 'y1');
  assert.equal(rows[2].r, null);
});

// ------------------------------------------------------------ HTTP + ndlangc

let base = '';
let server = null;
let info = null;

test.before(async () => {
  server = http.createServer(handle);
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  base = `http://127.0.0.1:${server.address().port}`;
  info = await (await fetch(`${base}/api/info`)).json();
});
test.after(() => server && server.close());

const stage = async (name, source, opt = 1) => {
  const r = await fetch(`${base}/api/stage`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ stage: name, source, opt }),
  });
  return { http: r.status, ...(await r.json()) };
};

const needsCompiler = (t) => {
  if (!info.compiler || !info.backend) {
    t.skip('ndlangc with the LLVM backend not found');
    return true;
  }
  return false;
};

test('serves the page and refuses paths outside public/', async () => {
  assert.equal((await fetch(`${base}/`)).status, 200);
  assert.equal((await fetch(`${base}/app.js`)).headers.get('content-type'), 'text/javascript; charset=utf-8');
  assert.notEqual((await fetch(`${base}/%2e%2e/server.js`)).status, 200);
});

test('lists the example programs', async () => {
  const ex = await (await fetch(`${base}/api/examples`)).json();
  assert.ok(ex.examples.some((p) => p.name === '02_dot_product'));
  assert.ok(ex.errors.some((p) => p.name === 'dim_mismatch'));
});

test('every stage of the dot-product example', async (t) => {
  if (needsCompiler(t)) return;
  const src = read('examples/02_dot_product.ndlang');

  const lex = await stage('lex', src);
  assert.equal(lex.status, 'ok');
  assert.match(lex.output, /^7:5\tKW_LET\tlet$/m);

  const parse = await stage('parse', src);
  assert.match(parse.output, /^\s+BinaryOp: \*$/m); // no types before sema

  const sema = await stage('sema', src);
  assert.match(sema.output, /BinaryOp: \* : vec<3>/);

  const ir = await stage('irgen', src);
  assert.match(ir.output, /alloca <3 x float>/);

  const opt = await stage('opt', src);
  assert.deepEqual(Object.keys(opt.output), ['O0', 'O1', 'O2']);
  assert.doesNotMatch(opt.output.O1.split('define i32 @main')[0], /alloca/); // mem2reg ran

  const backend = await stage('backend', src);
  assert.match(backend.output.asm, /^dot:/m);

  const run = await stage('run', src);
  assert.equal(run.status, 'ok');
  assert.equal(run.output.native.stdout, '32.000000\n');
  assert.equal(run.output.match, true);

  assert.equal((await stage('lex', src)).cached, true);
});

test('a semantic error stops at sema with a located diagnostic', async (t) => {
  if (needsCompiler(t)) return;
  const res = await stage('sema', read('tests/errors/dim_mismatch.ndlang'));
  assert.equal(res.status, 'error');
  assert.equal(res.diagnostics[0].line, 5);
  assert.equal(res.diagnostics[0].col, 13);
  assert.match(res.diagnostics[0].message, /vector dimension mismatch/);
});

test('rejects unknown stages and oversized input', async () => {
  assert.equal((await stage('nope', 'x')).http, 400);
  if (info.compiler) assert.equal((await stage('lex', 'x'.repeat(70 * 1024))).http, 400);
});
