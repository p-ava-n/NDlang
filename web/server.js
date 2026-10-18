#!/usr/bin/env node
// NDLang explorer: a small local web server around `ndlangc`.
//
//   node web/server.js            then open http://localhost:8090
//
// Every pipeline stage is a real `ndlangc` invocation (--emit-tokens,
// --emit-parse, --emit-ast, --emit-ir, --emit-asm, a full build + run), one
// request per stage, so the UI's progress indicator reflects actual compiler
// work. Results are cached by (stage, optimisation level, source).
//
// Environment: PORT (default 8090), HOST (default 127.0.0.1), NDLANGC (path
// to the compiler; default build/ndlangc or ~/ndlang-build/ndlangc).
// No npm dependencies; needs Node 18+.

'use strict';

const http = require('http');
const fs = require('fs');
const fsp = require('fs/promises');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const PUBLIC = path.join(__dirname, 'public');
const PORT = Number(process.env.PORT || 8090);
const HOST = process.env.HOST || '127.0.0.1';

const MAX_SOURCE = 64 * 1024;
const MAX_OUTPUT = 1024 * 1024;
const COMPILE_TIMEOUT_MS = 20000;
const RUN_TIMEOUT_MS = 5000;
const CACHE_LIMIT = 200;

const STAGES = ['lex', 'parse', 'sema', 'irgen', 'opt', 'backend', 'run'];

function findCompiler() {
  const exe = process.platform === 'win32' ? 'ndlangc.exe' : 'ndlangc';
  const candidates = [
    process.env.NDLANGC,
    path.join(ROOT, 'build', exe),
    path.join(os.homedir(), 'ndlang-build', exe),
  ].filter(Boolean);
  return candidates.find((p) => fs.existsSync(p)) || null;
}
const NDLANGC = findCompiler();

// ---------------------------------------------------------------- processes

// Runs `file args` without a shell. Never rejects: failures come back as data.
function run(file, args, { cwd, timeout }) {
  return new Promise((resolve) => {
    const t0 = process.hrtime.bigint();
    execFile(file, args, { cwd, timeout, killSignal: 'SIGKILL', maxBuffer: MAX_OUTPUT, env: { PATH: process.env.PATH } },
      (err, stdout, stderr) => {
        const ms = Number(process.hrtime.bigint() - t0) / 1e6;
        let code = 0;
        if (err) code = typeof err.code === 'number' ? err.code : -1;
        const timedOut = Boolean(err && err.killed);
        const overflow = Boolean(err && err.code === 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER');
        resolve({ code, stdout: String(stdout), stderr: String(stderr), ms, timedOut, overflow });
      });
  });
}

const DIAG_RE = /^.*?:(\d+):(\d+): error: (.*)$/;

// ndlangc reports "file:line:col: error: msg"; turn that into data.
function parseDiagnostics(stderr) {
  const diags = [];
  for (const line of stderr.split('\n')) {
    const m = DIAG_RE.exec(line);
    if (m) diags.push({ line: Number(m[1]), col: Number(m[2]), message: m[3] });
    else if (/^(error|runtime error): /.test(line)) diags.push({ line: 0, col: 0, message: line.replace(/^error: /, '') });
  }
  return diags;
}

async function compileStep(dir, args) {
  const r = await run(NDLANGC, ['program.ndlang', ...args], { cwd: dir, timeout: COMPILE_TIMEOUT_MS });
  return { ...r, diagnostics: r.code === 0 ? [] : parseDiagnostics(r.stderr) };
}

function stageError(r, extra = {}) {
  const diagnostics = r.diagnostics && r.diagnostics.length ? r.diagnostics
    : [{ line: 0, col: 0, message: r.timedOut ? 'ndlangc timed out' : (r.stderr.trim() || `ndlangc exited with ${r.code}`) }];
  return { status: 'error', ms: r.ms, diagnostics, stderr: r.stderr, ...extra };
}

// One pipeline stage. Returns { status, ms, output, diagnostics, ... }.
async function runStage(stage, source, opt) {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'ndlang-web-'));
  try {
    await fsp.writeFile(path.join(dir, 'program.ndlang'), source);
    const simple = { lex: ['--emit-tokens'], parse: ['--emit-parse'], sema: ['--emit-ast'], irgen: ['--emit-ir', '-O0'] };

    if (simple[stage]) {
      const r = await compileStep(dir, simple[stage]);
      return r.code === 0 ? { status: 'ok', ms: r.ms, output: r.stdout, diagnostics: [] } : stageError(r);
    }

    if (stage === 'opt') {
      const out = {};
      let ms = 0;
      for (const level of [0, 1, 2]) {
        const r = await compileStep(dir, ['--emit-ir', `-O${level}`]);
        ms += r.ms;
        if (r.code !== 0) return stageError(r);
        out[`O${level}`] = r.stdout;
      }
      return { status: 'ok', ms, output: out, diagnostics: [] };
    }

    if (stage === 'backend') {
      const ir = await compileStep(dir, ['--emit-ir', `-O${opt}`]);
      if (ir.code !== 0) return stageError(ir);
      const asm = await compileStep(dir, ['--emit-asm', `-O${opt}`]);
      if (asm.code !== 0) return stageError(asm);
      return { status: 'ok', ms: ir.ms + asm.ms, output: { ir: ir.stdout, asm: asm.stdout }, diagnostics: [] };
    }

    if (stage === 'run') {
      const build = await compileStep(dir, [`-O${opt}`, '-o', 'program']);
      if (build.code !== 0) return stageError(build);
      const native = await run(path.join(dir, 'program'), [], { cwd: dir, timeout: RUN_TIMEOUT_MS });
      // If the native program never finished, the interpreter (slower) won't either.
      const interp = native.timedOut
        ? { stdout: '', stderr: '', code: null, ms: 0, timedOut: false, skipped: true }
        : await run(NDLANGC, ['program.ndlang', '--interpret'], { cwd: dir, timeout: RUN_TIMEOUT_MS });
      const pick = (r) => ({ stdout: r.stdout, stderr: r.stderr, exitCode: r.code, ms: r.ms, timedOut: r.timedOut, skipped: Boolean(r.skipped) });
      const output = {
        buildMs: build.ms,
        native: pick(native),
        interp: pick(interp),
        match: !native.timedOut && !interp.timedOut && native.stdout === interp.stdout,
      };
      if (native.timedOut) {
        return { status: 'error', ms: build.ms + native.ms, output,
                 diagnostics: [{ line: 0, col: 0, message: `program killed after ${RUN_TIMEOUT_MS / 1000} s (infinite loop?)` }] };
      }
      return { status: 'ok', ms: build.ms + native.ms, output, diagnostics: [] };
    }

    return { status: 'error', ms: 0, diagnostics: [{ line: 0, col: 0, message: `unknown stage '${stage}'` }] };
  } finally {
    fsp.rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}

// ---------------------------------------------------------------- cache

// Map keeps insertion order, so re-inserting on hit gives a simple LRU.
const cache = new Map();

function cacheKey(stage, opt, source) {
  // Only the backend and run stages depend on the chosen -O level; the
  // optimizer stage always produces -O0, -O1 and -O2.
  const level = stage === 'backend' || stage === 'run' ? opt : '-';
  return crypto.createHash('sha256').update(`${stage}\0${level}\0${source}`).digest('hex');
}

async function cachedStage(stage, source, opt) {
  const key = cacheKey(stage, opt, source);
  if (cache.has(key)) {
    const hit = cache.get(key);
    cache.delete(key);
    cache.set(key, hit);
    return { ...hit, cached: true };
  }
  const result = await runStage(stage, source, opt);
  // A timed-out run might just mean the machine was busy; don't pin it.
  if (!(stage === 'run' && result.status === 'error')) {
    cache.set(key, result);
    if (cache.size > CACHE_LIMIT) cache.delete(cache.keys().next().value);
  }
  return { ...result, cached: false };
}

// ---------------------------------------------------------------- examples

async function readNdl(dir) {
  let names = [];
  try { names = (await fsp.readdir(dir)).filter((n) => n.endsWith('.ndlang')).sort(); } catch { return []; }
  return Promise.all(names.map(async (n) => ({ name: n.replace(/\.ndlang$/, ''), source: await fsp.readFile(path.join(dir, n), 'utf8') })));
}

async function examples() {
  return {
    examples: await readNdl(path.join(ROOT, 'examples')),
    benchmarks: await readNdl(path.join(ROOT, 'benchmarks')),
    errors: await readNdl(path.join(ROOT, 'tests', 'errors')),
  };
}

let infoCache = null;
async function info() {
  if (infoCache) return infoCache;
  const result = { compiler: NDLANGC, target: null, stages: STAGES, runTimeoutMs: RUN_TIMEOUT_MS };
  if (NDLANGC) {
    const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'ndlang-web-'));
    await fsp.writeFile(path.join(dir, 'program.ndlang'), 'func main() -> int { return 0; }\n');
    const r = await compileStep(dir, ['--emit-ir', '-O0']);
    fsp.rm(dir, { recursive: true, force: true }).catch(() => {});
    const m = /target triple = "([^"]+)"/.exec(r.stdout);
    result.target = m ? m[1] : null;
    result.backend = r.code === 0;
  }
  infoCache = result;
  return result;
}

// ---------------------------------------------------------------- http

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml', '.ico': 'image/x-icon' };

function send(res, status, body, type = 'application/json; charset=utf-8') {
  const data = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
  res.end(data);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (c) => {
      size += c.length;
      if (size > MAX_SOURCE * 2) { reject(new Error('request too large')); req.destroy(); return; }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

async function serveStatic(req, res) {
  const url = new URL(req.url, 'http://x');
  const rel = url.pathname === '/' ? 'index.html' : decodeURIComponent(url.pathname).replace(/^\/+/, '');
  const file = path.resolve(PUBLIC, rel);
  if (!file.startsWith(PUBLIC + path.sep)) return send(res, 403, { error: 'forbidden' });
  try {
    const data = await fsp.readFile(file);
    send(res, 200, data, TYPES[path.extname(file)] || 'application/octet-stream');
  } catch {
    send(res, 404, { error: 'not found' });
  }
}

async function handle(req, res) {
  const { pathname } = new URL(req.url, 'http://x');
  try {
    if (req.method === 'GET' && pathname === '/api/info') return send(res, 200, await info());
    if (req.method === 'GET' && pathname === '/api/examples') return send(res, 200, await examples());
    if (req.method === 'POST' && pathname === '/api/stage') {
      if (!NDLANGC) return send(res, 503, { error: 'ndlangc not found: build it, or set NDLANGC=/path/to/ndlangc' });
      const body = JSON.parse(await readBody(req));
      const { stage, source } = body;
      const opt = body.opt === 2 ? 2 : 1;
      if (!STAGES.includes(stage)) return send(res, 400, { error: `unknown stage '${stage}'` });
      if (typeof source !== 'string' || source.length > MAX_SOURCE) return send(res, 400, { error: 'source must be a string under 64 KB' });
      return send(res, 200, { stage, opt, ...(await cachedStage(stage, source, opt)) });
    }
    if (req.method === 'GET') return serveStatic(req, res);
    send(res, 405, { error: 'method not allowed' });
  } catch (e) {
    send(res, 500, { error: String(e.message || e) });
  }
}

if (require.main === module) {
  http.createServer(handle).listen(PORT, HOST, () => {
    console.log(`NDLang explorer on http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${PORT}`);
    console.log(NDLANGC ? `using ${NDLANGC}` : 'warning: ndlangc not found (set NDLANGC)');
  });
}

module.exports = { handle, runStage, parseDiagnostics, STAGES };
