// NDLang explorer: walks a program through every ndlangc stage and keeps the
// latest compile (source + every stage's output) so its earlier steps can be
// reviewed. Routes: #/source (the editor) and #/<stage>.

import { esc, hlNdl, hlIr, hlAsm, hlAst, hlPlain } from './highlight.js';
import { diffLines, alignRows } from './diff.js';

const STAGES = [
  { id: 'source', n: 0, name: 'source', title: 'source program' },
  { id: 'lex', n: 1, name: 'lexer', title: 'lexical analysis' },
  { id: 'parse', n: 2, name: 'parser', title: 'syntax analysis' },
  { id: 'sema', n: 3, name: 'semantic', title: 'semantic analysis' },
  { id: 'irgen', n: 4, name: 'ir gen', title: 'LLVM IR generation' },
  { id: 'opt', n: 5, name: 'optimizer', title: 'optimization' },
  { id: 'backend', n: 6, name: 'backend', title: 'code generation' },
  { id: 'run', n: 7, name: 'link + run', title: 'link and execute' },
];
const IDS = STAGES.map((s) => s.id);
const PIPE = STAGES.slice(1);
const byId = Object.fromEntries(STAGES.map((s) => [s.id, s]));

const TEMPLATE = 'func main() -> int {\n    let v: vec<3> = [1.0, 2.0, 3.0];\n    print(sum(v * 2.0));\n    return 0;\n}\n';
const PASSES = {
  1: 'mem2reg → instcombine → simplifycfg → slp-vectorizer → instcombine',
  2: "LLVM's default -O2 pipeline (inlining, SROA, GVN, LICM, loop unrolling, loop + SLP vectorizers, …)",
};

// ------------------------------------------------------------------ storage

const canStore = (() => {
  try {
    localStorage.setItem('ndlang.probe', '1');
    localStorage.removeItem('ndlang.probe');
    return true;
  } catch {
    return false;
  }
})();
const store = {
  get(key, fallback) {
    if (!canStore) return fallback;
    try {
      const v = localStorage.getItem(key);
      return v == null ? fallback : JSON.parse(v);
    } catch {
      return fallback;
    }
  },
  set(key, value) {
    if (!canStore) return true;
    try {
      localStorage.setItem(key, JSON.stringify(value));
      return true;
    } catch {
      return false;
    }
  },
};

// -------------------------------------------------------------------- state

const state = {
  draft: store.get('ndlang.draft', null),
  draftName: store.get('ndlang.draftName', 'untitled'),
  draftBase: store.get('ndlang.draftBase', null), // source the draft was loaded from
  opt: store.get('ndlang.opt', 1) === 2 ? 2 : 1,
  run: null, // the latest compile, shown in the pipeline
  stage: 'source', // stage on screen
  busy: false,
  follow: true, // view follows the pipeline while it runs
  info: null,
  examples: null,
  optAfter: null, // optimizer view: -O0 vs this level (default: the run's level)
  showDiff: true,
  showDirectives: false,
  flash: '',
};

// The latest compile survives a reload; nothing older is kept.
function loadRun() {
  const run = store.get('ndlang.run', null);
  if (!run || typeof run.source !== 'string' || !run.stages) return null;
  // A page closed mid-compile leaves stages "running"; they never finished.
  for (const s of PIPE) if (!run.stages[s.id] || run.stages[s.id].status === 'running') run.stages[s.id] = { status: 'pending' };
  return run;
}
if (canStore) {
  // Older versions kept a run history; drop it.
  localStorage.removeItem('ndlang.runs');
  localStorage.removeItem('ndlang.seq');
}
state.run = loadRun();

const saveRun = () => store.set('ndlang.run', state.run);

let draftTimer = 0;
function saveDraft() {
  clearTimeout(draftTimer);
  draftTimer = setTimeout(() => {
    store.set('ndlang.draft', state.draft);
    store.set('ndlang.draftName', state.draftName);
    store.set('ndlang.draftBase', state.draftBase);
  }, 250);
}

const currentRun = () => state.run;
const nextPending = (run) => PIPE.find((s) => run.stages[s.id].status === 'pending')?.id || null;
const failedStage = (run) => PIPE.find((s) => run.stages[s.id].status === 'error')?.id || null;
const draftModified = () => state.draft !== state.draftBase;

// "name*" for an edited example, "untitled" once most of it is rewritten.
function draftLabel() {
  if (!draftModified()) return state.draftName;
  const base = new Set((state.draftBase || '').split('\n').map((s) => s.trim()).filter(Boolean));
  const lines = state.draft.split('\n').map((s) => s.trim()).filter(Boolean);
  const kept = lines.filter((l) => base.has(l)).length;
  return state.draftName !== 'untitled' && kept * 2 >= lines.length ? `${state.draftName}*` : 'untitled';
}

// ------------------------------------------------------------------ helpers

const $ = (sel) => document.querySelector(sel);

function h(tag, props, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'html') e.innerHTML = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat()) if (c != null && c !== false) e.append(c instanceof Node ? c : String(c));
  return e;
}

const splitLines = (s) => (s || '').replace(/\n$/, '').split('\n');
const fmtMs = (ms) => (ms == null ? '' : ms < 10 ? `${ms.toFixed(1)} ms` : ms < 1000 ? `${Math.round(ms)} ms` : `${(ms / 1000).toFixed(2)} s`);
const clock = (t) => new Date(t).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });

function flash(msg) {
  state.flash = msg;
  renderStatus();
  setTimeout(() => {
    if (state.flash === msg) {
      state.flash = '';
      renderStatus();
    }
  }, 3500);
}

function win(title, { meta = '', tools = [], body, cls = '' } = {}) {
  return h('section', { class: `win ${cls}` },
    h('div', { class: 'win-title' }, h('span', { class: 't' }, title), h('span', { class: 'm' }, meta), h('span', { class: 'tools' }, ...tools)),
    h('div', { class: 'win-body' }, body));
}

function copyTool(text) {
  return h('button', {
    type: 'button',
    class: 'link',
    onclick: () => navigator.clipboard?.writeText(text).then(() => flash('copied to clipboard'), () => flash('copy failed')),
  }, 'copy');
}

// One div per line so individual lines can be marked.
function codeHtml(lines, hl, { lineClass = () => '', tag = () => '' } = {}) {
  let out = '';
  lines.forEach((raw, i) => {
    const n = i + 1;
    const t = tag(n, raw);
    out += `<div class="ln ${lineClass(n, raw)}" data-n="${n}"><span class="no">${n}</span>`
      + `<span class="tx">${hl(raw)}${t ? `<span class="tag">${esc(t)}</span>` : ''}</span></div>`;
  });
  return out;
}
const codeBox = (lines, hl, opts) => h('div', { class: 'code', html: codeHtml(lines, hl, opts) });

function codeWin(title, text, hl, { meta = '', opts, cls = 'grow' } = {}) {
  return win(title, { meta, cls, tools: [copyTool(text)], body: codeBox(splitLines(text), hl, opts) });
}

// Functions and instruction counts in a textual LLVM module.
function irFunctions(ir) {
  const out = [];
  let cur = null;
  for (const line of splitLines(ir)) {
    const m = /^define\b.*?@([\w.$-]+)\(/.exec(line);
    if (m) {
      cur = { name: m[1], count: 0 };
      out.push(cur);
    } else if (cur) {
      if (line === '}') cur = null;
      else if (/^\s+[^\s;]/.test(line)) cur.count++;
    }
  }
  return out;
}
const asmInstructions = (lines) => lines.filter((l) => /^\s+[a-z]/.test(l)).length;

function cmdFor(id, run) {
  const o = run ? run.opt : state.opt;
  return {
    lex: 'ndlangc program.ndlang --emit-tokens',
    parse: 'ndlangc program.ndlang --emit-parse',
    sema: 'ndlangc program.ndlang --emit-ast',
    irgen: 'ndlangc program.ndlang --emit-ir -O0',
    opt: 'ndlangc program.ndlang --emit-ir -O0 | -O1 | -O2',
    backend: `ndlangc program.ndlang --emit-asm -O${o}`,
    run: `ndlangc program.ndlang -O${o} -o program && ./program`,
  }[id] || '';
}

// ------------------------------------------------------------------ routing

const parseHash = () => {
  const stage = location.hash.replace(/^#\/?/, '');
  return IDS.includes(stage) ? stage : 'source';
};

function go(stage, { replace = false } = {}) {
  const hash = `#/${stage}`;
  if (location.hash !== hash) {
    if (replace) history.replaceState(null, '', hash);
    else history.pushState(null, '', hash);
  }
  route();
}

function route() {
  const stage = parseHash();
  // Stage views need a compile; without one, fall back to the editor.
  if (stage !== 'source' && !state.run) {
    history.replaceState(null, '', '#/source');
    state.stage = 'source';
  } else {
    state.stage = stage;
  }
  render();
}

function stepView(delta) {
  const i = IDS.indexOf(state.stage) + delta;
  if (i < 0 || i >= IDS.length || (IDS[i] !== 'source' && !state.run)) return;
  go(IDS[i]);
}

// ------------------------------------------------------------------ running

// A new compile replaces the previous one.
function newRun(source) {
  state.run = {
    name: draftLabel(),
    created: Date.now(),
    source,
    opt: state.opt,
    stages: Object.fromEntries(PIPE.map((s) => [s.id, { status: 'pending' }])),
  };
  saveRun();
  return state.run;
}

async function execStage(run, id) {
  run.stages[id] = { status: 'running' };
  render();
  let res;
  try {
    const r = await fetch('api/stage', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ stage: id, source: run.source, opt: run.opt }),
    });
    const body = await r.json();
    res = r.ok ? body : { status: 'error', diagnostics: [{ line: 0, col: 0, message: body.error || `HTTP ${r.status}` }] };
  } catch {
    res = { status: 'error', diagnostics: [{ line: 0, col: 0, message: 'cannot reach the explorer server (is `node web/server.js` running?)' }] };
  }
  run.stages[id] = { status: res.status, ms: res.ms, output: res.output, diagnostics: res.diagnostics || [], cached: Boolean(res.cached) };
  if (res.status === 'error') {
    for (const s of PIPE) if (run.stages[s.id].status === 'pending') run.stages[s.id] = { status: 'skipped' };
  }
  if (run === state.run) saveRun();
  render();
  return res.status === 'ok';
}

// Runs the remaining stages of `run`: all of them, or just the next one.
async function advance(run, mode) {
  if (state.busy) return;
  state.busy = true;
  state.follow = true;
  let first = true;
  try {
    for (let id = nextPending(run); id; id = nextPending(run)) {
      if (state.follow) {
        go(id, { replace: !first });
        first = false;
      }
      const ok = await execStage(run, id);
      if (!ok || mode === 'step') break;
    }
  } finally {
    state.busy = false;
    render();
  }
}

// Compiles the editor's program. Continues the current compile when it is the
// same program at the same -O level and still has stages left; otherwise
// starts a new one, so "run" always goes through to execution.
async function compile(mode) {
  if (state.busy) return;
  const source = state.draft;
  if (!source.trim()) {
    flash('nothing to compile');
    return;
  }
  let run = currentRun();
  const same = run && run.source === source && run.opt === state.opt;
  if (!(same && nextPending(run))) run = newRun(source);
  await advance(run, mode);
}

function loadIntoEditor(run) {
  if (!confirmReplaceDraft()) return false;
  state.draft = run.source;
  state.draftName = run.name.replace(/\*$/, '');
  state.draftBase = run.source;
  state.opt = run.opt;
  store.set('ndlang.opt', state.opt);
  saveDraft();
  if (editor) editor.setValue(state.draft);
  return true;
}

// The draft is safe to replace if it is unchanged or is the compiled program.
function confirmReplaceDraft() {
  if (!draftModified() || state.run?.source === state.draft) return true;
  return confirm('Replace the program in the editor? Your changes have not been compiled and will be lost.');
}

// ------------------------------------------------------------------- editor

let editor = null;

function createEditor() {
  const gutter = h('div', { class: 'gutter', 'aria-hidden': 'true' });
  const pre = h('pre', { 'aria-hidden': 'true' });
  const ta = h('textarea', { spellcheck: 'false', autocapitalize: 'off', autocomplete: 'off', autocorrect: 'off', wrap: 'off', 'aria-label': 'NDLang source' });
  const root = h('div', { class: 'editor' }, gutter, h('div', { class: 'code-wrap' }, pre, ta));
  let errs = new Map();

  const sync = () => {
    pre.scrollTop = ta.scrollTop;
    pre.scrollLeft = ta.scrollLeft;
    gutter.scrollTop = ta.scrollTop;
  };
  const paint = () => {
    const lines = ta.value.split('\n');
    pre.innerHTML = lines.map(hlNdl).join('\n') + '\n\n\n';
    gutter.innerHTML = lines.map((_, i) => {
      const m = errs.get(i + 1);
      return m ? `<span class="e" title="${esc(m)}">${i + 1}</span>` : `<span>${i + 1}</span>`;
    }).join('') + '<span></span><span></span>';
    sync();
  };
  const insert = (text) => {
    ta.focus();
    // execCommand keeps the browser's undo stack; fall back if it is gone.
    if (!document.execCommand('insertText', false, text)) {
      ta.setRangeText(text, ta.selectionStart, ta.selectionEnd, 'end');
      ta.dispatchEvent(new Event('input'));
    }
  };

  ta.addEventListener('scroll', sync);
  ta.addEventListener('input', () => {
    state.draft = ta.value;
    saveDraft();
    paint();
    renderEditorChrome();
    renderStatus();
    renderPipeline();
  });
  ta.addEventListener('keydown', (e) => {
    if (e.key === 'Tab' && !e.ctrlKey && !e.altKey) {
      e.preventDefault();
      insert('    ');
    } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey && !e.shiftKey) {
      e.preventDefault();
      const before = ta.value.slice(0, ta.selectionStart);
      const line = before.slice(before.lastIndexOf('\n') + 1);
      const indent = /^\s*/.exec(line)[0] + (/\{\s*$/.test(line) ? '    ' : '');
      insert('\n' + indent);
    } else if (e.key === 'Escape') {
      ta.blur();
    }
  });

  return {
    root,
    textarea: ta,
    setValue(v) {
      ta.value = v;
      paint();
    },
    setErrors(map) {
      errs = map;
      paint();
    },
    goto(line, col) {
      const lines = ta.value.split('\n');
      let off = 0;
      for (let i = 0; i < Math.min(line - 1, lines.length); i++) off += lines[i].length + 1;
      off += Math.max(0, col - 1);
      ta.focus();
      ta.setSelectionRange(off, off);
      ta.scrollTop = Math.max(0, (line - 4) * parseFloat(getComputedStyle(ta).lineHeight));
      sync();
    },
  };
}

// Diagnostics from the latest compile, if it failed on exactly this program.
function draftDiagnostics() {
  const run = state.run;
  if (!run || run.source !== state.draft) return null;
  const f = failedStage(run);
  return f ? { run, stage: f, diags: run.stages[f].diagnostics } : null;
}

let editorChrome = null;

function renderEditorChrome() {
  if (!editorChrome) return;
  const lines = state.draft.split('\n').length;
  editorChrome.meta.textContent = `${draftLabel()} · ${lines} line${lines === 1 ? '' : 's'}`;
  const d = draftDiagnostics();
  editor.setErrors(new Map((d?.diags || []).filter((x) => x.line).map((x) => [x.line, x.message])));
  editorChrome.diags.replaceChildren(d ? diagnosticsWin(d.run, d.diags, `from the last compile · ${byId[d.stage].name}`) : '');
}

function examplesSelect() {
  const sel = h('select', { 'aria-label': 'load an example program' }, h('option', { value: '' }, 'load example…'));
  const ex = state.examples || {};
  const groups = [['examples', 'examples'], ['benchmarks', 'benchmarks'], ['errors', 'programs that must fail']];
  for (const [key, label] of groups) {
    if (!ex[key]?.length) continue;
    sel.append(h('optgroup', { label }, ex[key].map((p) => h('option', { value: `${key}/${p.name}` }, p.name))));
  }
  sel.addEventListener('change', () => {
    const [key, name] = sel.value.split('/');
    const prog = ex[key]?.find((p) => p.name === name);
    sel.value = '';
    if (!prog || !confirmReplaceDraft()) return;
    state.draft = prog.source;
    state.draftName = prog.name;
    state.draftBase = prog.source;
    saveDraft();
    editor.setValue(state.draft);
    renderEditorChrome();
    renderStatus();
    editor.textarea.focus();
  });
  return sel;
}

function viewSource() {
  if (!editor) {
    editor = createEditor();
    editor.setValue(state.draft);
  }
  const meta = h('span');
  const newBtn = h('button', {
    type: 'button',
    class: 'link',
    onclick: () => {
      if (!confirmReplaceDraft()) return;
      state.draft = TEMPLATE;
      state.draftName = 'untitled';
      state.draftBase = TEMPLATE;
      saveDraft();
      editor.setValue(state.draft);
      renderEditorChrome();
      editor.textarea.focus();
    },
  }, 'new');
  const diags = h('div', { class: 'diags' });
  editorChrome = { meta, diags };
  const edWin = h('section', { class: 'win grow' },
    h('div', { class: 'win-title' }, h('span', { class: 't' }, 'program.ndlang'), h('span', { class: 'm' }, meta),
      h('span', { class: 'tools' }, examplesSelect(), newBtn)),
    editor.root);
  renderEditorChrome();
  return [
    h('div', { class: 'stage-head' },
      h('h1', {}, h('span', { class: 'n' }, '[0/7] '), 'source program'),
      h('span', { class: 'dim' }, 'write NDLang here, or load an example'),
      navButtons()),
    h('div', { class: 'stack' }, edWin, diags),
  ];
}

// ------------------------------------------------------------- stage views

function navButtons() {
  const i = IDS.indexOf(state.stage);
  const run = currentRun();
  const can = (j) => j >= 0 && j < IDS.length && (IDS[j] === 'source' || run);
  return h('span', { class: 'nav' },
    h('button', { type: 'button', disabled: !can(i - 1), onclick: () => stepView(-1), title: 'previous stage (←)' }, '◂ prev'),
    h('button', { type: 'button', disabled: !can(i + 1), onclick: () => stepView(1), title: 'next stage (→)' }, 'next ▸'));
}

function stageHead(run, st) {
  const res = run.stages[st.id];
  const statusEl = {
    ok: () => h('span', { class: 'ok' }, `✓ ${fmtMs(res.ms)}${res.cached ? ' · cached' : ''}`),
    error: () => h('span', { class: 'err' }, '✗ failed'),
    running: () => h('span', { class: 'warn' }, h('span', { class: 'spin' }), ' running'),
    skipped: () => h('span', { class: 'dim' }, '– skipped'),
    pending: () => h('span', { class: 'dim' }, '· not run yet'),
  }[res.status]();
  // The editor may have changed since this program was compiled.
  const load = run.source !== state.draft && h('button', {
    type: 'button',
    class: 'link',
    title: 'the editor has changed since this compile; copy the compiled program back',
    onclick: () => {
      if (loadIntoEditor(run)) go('source');
    },
  }, 'load into editor');
  return h('div', { class: 'stage-head' },
    h('h1', {}, h('span', { class: 'n' }, `[${st.n}/7] `), st.title),
    h('span', { class: 'cmd' }, cmdFor(st.id, run)),
    statusEl,
    h('span', { class: 'nav' }, load, ...navButtons().childNodes));
}

function diagnosticsWin(run, diags, meta) {
  const list = h('div', { class: 'diag-list' }, diags.map((d) => h('button', {
    type: 'button',
    class: 'diag',
    title: d.line ? 'show in the editor' : null,
    onclick: () => {
      if (!d.line) return;
      if (run.source !== state.draft && !loadIntoEditor(run)) return;
      go('source');
      editor.goto(d.line, d.col);
    },
  },
  d.line ? h('span', { class: 'loc' }, `program.ndlang:${d.line}:${d.col}: `) : '',
  h('span', { class: 'lbl' }, 'error: '), d.message)));
  return win('diagnostics', { meta: meta ?? `${diags.length} error${diags.length === 1 ? '' : 's'}`, body: list });
}

function errorView(run, res) {
  const byLine = new Map();
  for (const d of res.diagnostics) if (d.line) byLine.set(d.line, d);
  // The source with each error line marked and a caret row under it.
  let html = '';
  splitLines(run.source).forEach((raw, i) => {
    const n = i + 1;
    const d = byLine.get(n);
    html += `<div class="ln ${d ? 'errline' : ''}"><span class="no">${n}</span><span class="tx">${hlNdl(raw)}</span></div>`;
    if (d) html += `<div class="ln caret"><span class="no"></span><span class="tx">${esc(' '.repeat(Math.max(0, d.col - 1)))}^ ${esc(d.message)}</span></div>`;
  });
  return h('div', { class: 'split' },
    win('source · program.ndlang', { cls: 'grow', meta: `${byLine.size} line${byLine.size === 1 ? '' : 's'} marked`, body: h('div', { class: 'code', html }) }),
    diagnosticsWin(run, res.diagnostics));
}

function notice(...kids) {
  return h('div', { class: 'notice' }, ...kids);
}

function viewLex(run, res) {
  const toks = splitLines(res.output).map((l) => {
    const [pos, kind, text = ''] = l.split('\t');
    const [line, col] = pos.split(':').map(Number);
    return { line, col, kind, text };
  });
  const kindClass = (k) => (/^KW_(INT|FLOAT|BOOL|VEC)$/.test(k) ? 'k-ty' : /^KW_(TRUE|FALSE)$/.test(k) || /_LIT$/.test(k) ? 'k-lit'
    : /^KW_/.test(k) ? 'k-kw' : k === 'IDENT' ? 'k-id' : k === 'T_EOF' ? 'k-eof' : 'k-punct');
  const rows = toks.map((t, i) => `<tr data-i="${i}"><td>${t.line}:${t.col}</td><td class="${kindClass(t.kind)}">${t.kind}</td><td>${esc(t.text)}</td></tr>`).join('');
  const table = h('table', { class: 'tok', html: `<tbody>${rows}</tbody>` });
  const srcLines = splitLines(run.source);
  const src = codeBox(srcLines, hlNdl);

  // Hovering a token marks it in the source.
  let marked = null;
  const restore = () => {
    if (!marked) return;
    marked.querySelector('.tx').innerHTML = hlNdl(srcLines[Number(marked.dataset.n) - 1] || '');
    marked.classList.remove('mark');
    marked = null;
  };
  table.addEventListener('mouseover', (e) => {
    const tr = e.target.closest('tr');
    if (!tr) return;
    restore();
    const t = toks[Number(tr.dataset.i)];
    const lineEl = src.querySelector(`.ln[data-n="${t.line}"]`);
    if (!lineEl || !t.text) return;
    const raw = srcLines[t.line - 1];
    const a = t.col - 1;
    const b = a + t.text.length;
    lineEl.querySelector('.tx').innerHTML = `${esc(raw.slice(0, a))}<mark>${esc(raw.slice(a, b))}</mark>${esc(raw.slice(b))}`;
    lineEl.classList.add('mark');
    lineEl.scrollIntoView({ block: 'nearest' });
    marked = lineEl;
  });
  table.addEventListener('mouseleave', restore);

  return h('div', { class: 'split' },
    win('input · source', { cls: 'grow', meta: `${run.source.length} characters`, body: src }),
    win('output · tokens', { cls: 'grow', meta: `${toks.length} tokens · hover one to find it`, tools: [copyTool(res.output)], body: table }));
}

function viewParse(run, res) {
  return h('div', { class: 'split' },
    codeWin('input · source', run.source, hlNdl),
    codeWin('output · AST (parser, no types yet)', res.output, hlAst, { meta: `${splitLines(res.output).length} nodes` }));
}

function viewSema(run, res) {
  const parsed = run.stages.parse.output || '';
  const casts = splitLines(res.output).filter((l) => l.includes('CastExpr')).length;
  return h('div', { class: 'split' },
    codeWin('input · AST from the parser', parsed, hlAst),
    codeWin('output · typed AST', res.output, hlAst, {
      meta: `0 errors · ${casts} implicit int→float cast${casts === 1 ? '' : 's'} inserted`,
      opts: { lineClass: (n, l) => (l.includes('CastExpr') ? 'note' : ''), tag: (n, l) => (l.includes('CastExpr') ? '◂ inserted by sema' : '') },
    }));
}

function viewIrgen(run, res) {
  const fns = irFunctions(res.output);
  const n = fns.reduce((a, f) => a + f.count, 0);
  return h('div', { class: 'split' },
    codeWin('input · typed AST', run.stages.sema.output || '', hlAst),
    codeWin('output · LLVM IR (-O0)', res.output, hlIr, { meta: `${fns.length} functions · ${n} instructions` }));
}

function segButtons(levels, value, onPick) {
  return h('span', { class: 'seg' }, levels.map((lv) => h('button', {
    type: 'button',
    class: lv === value ? 'on' : '',
    onclick: () => onPick(lv),
  }, `-O${lv}`)));
}

function viewOpt(run, res) {
  const level = state.optAfter || run.opt;
  const before = res.output.O0;
  const after = res.output[`O${level}`];
  const fb = irFunctions(before);
  const fa = irFunctions(after);
  const total = (fs) => fs.reduce((a, f) => a + f.count, 0);

  const stat = (label, b, a) => {
    const pct = b ? Math.round(((a - b) / b) * 100) : 0;
    return h('span', {}, `${label} `, h('b', {}, String(b)), ' → ', h('b', {}, a == null ? 'gone' : String(a)),
      a == null ? '' : h('span', { class: a < b ? 'down' : a > b ? 'up' : '' }, ` (${pct > 0 ? '+' : ''}${pct}%)`));
  };
  const stats = h('div', { class: 'stats' },
    stat('instructions', total(fb), total(fa)),
    fb.map((f) => stat(`@${f.name}`, f.count, fa.find((x) => x.name === f.name)?.count ?? null)));

  const toolbar = h('div', { class: 'toolbar' },
    h('span', {}, 'after: ', segButtons([1, 2], level, (lv) => { state.optAfter = lv; render(true); })),
    h('button', { type: 'button', class: state.showDiff ? 'on' : '', onclick: () => { state.showDiff = !state.showDiff; render(true); } }, 'align + diff'),
    h('span', {}, `passes: ${PASSES[level]}`));

  let leftBody;
  let rightBody;
  if (state.showDiff) {
    const rows = alignRows(diffLines(splitLines(before), splitLines(after)));
    let ln = 0;
    let rn = 0;
    let L = '';
    let R = '';
    const gap = '<div class="ln gap"><span class="no"></span><span class="tx"> </span></div>';
    for (const row of rows) {
      L += row.l === null ? gap : `<div class="ln ${row.kind === 'eq' ? '' : row.kind === 'del' ? 'del' : 'chg-l'}"><span class="no">${++ln}</span><span class="tx">${hlIr(row.l)}</span></div>`;
      R += row.r === null ? gap : `<div class="ln ${row.kind === 'eq' ? '' : row.kind === 'add' ? 'add' : 'chg-r'}"><span class="no">${++rn}</span><span class="tx">${hlIr(row.r)}</span></div>`;
    }
    leftBody = h('div', { class: 'code', html: L });
    rightBody = h('div', { class: 'code', html: R });
  } else {
    leftBody = codeBox(splitLines(before), hlIr);
    rightBody = codeBox(splitLines(after), hlIr);
  }
  const left = win('before · -O0', { cls: 'grow', meta: `${total(fb)} instructions`, tools: [copyTool(before)], body: leftBody });
  const right = win(`after · -O${level}`, { cls: 'grow', meta: `${total(fa)} instructions`, tools: [copyTool(after)], body: rightBody });
  if (state.showDiff) {
    // Rows are aligned, so scroll both windows together.
    const a = left.querySelector('.win-body');
    const b = right.querySelector('.win-body');
    a.addEventListener('scroll', () => { if (b.scrollTop !== a.scrollTop) b.scrollTop = a.scrollTop; });
    b.addEventListener('scroll', () => { if (a.scrollTop !== b.scrollTop) a.scrollTop = b.scrollTop; });
  }
  return [toolbar, stats, h('div', { class: 'split' }, left, right)];
}

function viewBackend(run, res) {
  const all = splitLines(res.output.asm);
  const shown = state.showDirectives ? all : all.filter((l) => !/^\s+\./.test(l));
  const target = state.info?.target || 'host';
  const toolbar = h('div', { class: 'toolbar' },
    h('button', { type: 'button', class: state.showDirectives ? 'on' : '', onclick: () => { state.showDirectives = !state.showDirectives; render(true); } }, 'assembler directives'),
    h('span', {}, `TargetMachine for ${target} · instruction selection, register allocation, scheduling`));
  return [toolbar, h('div', { class: 'split' },
    codeWin(`input · optimized IR (-O${run.opt})`, res.output.ir, hlIr),
    win(`output · ${target.split('-')[0]} assembly`, {
      cls: 'grow',
      meta: `${asmInstructions(all)} instructions${state.showDirectives ? '' : ` · ${all.length - shown.length} directive lines hidden`}`,
      tools: [copyTool(res.output.asm)],
      body: codeBox(shown, hlAsm),
    }))];
}

function viewRun(run, res) {
  const o = res.output;
  let verdict;
  if (o.native.timedOut) verdict = h('div', { class: 'verdict err' }, `✗ the program was killed after ${(state.info?.runTimeoutMs || 5000) / 1000} s`);
  else if (o.interp.timedOut) verdict = h('div', { class: 'verdict warn' }, '… the reference interpreter timed out; native output only');
  else if (o.match) verdict = h('div', { class: 'verdict ok' }, '✓ native output matches the reference interpreter');
  else verdict = h('div', { class: 'verdict err' }, '✗ native output differs from the reference interpreter');

  const pane = (title, r, extra) => win(title, {
    cls: 'grow',
    meta: r.skipped ? 'not run' : r.timedOut ? 'timed out' : `exit ${r.exitCode} · ${fmtMs(r.ms)}`,
    tools: [copyTool(r.stdout)],
    body: h('div', {},
      h('div', { class: 'output', html: r.stdout ? esc(r.stdout) : '<span class="dim">(no output)</span>' }),
      r.stderr ? h('div', { class: 'output err' }, r.stderr) : '',
      h('div', { class: 'kv' }, ...extra.flatMap(([k, v]) => [h('span', {}, k), h('b', {}, v)]))),
  });
  return [verdict, h('div', { class: 'split' },
    pane(`native executable · -O${run.opt}`, o.native, [['built in', fmtMs(o.buildMs)], ['ran in', fmtMs(o.native.ms)], ['linker', 'clang / gcc via ndlangc']]),
    pane('reference interpreter · --interpret', o.interp, o.interp.skipped
      ? [['skipped', 'the native program did not finish']]
      : [['ran in', fmtMs(o.interp.ms)], ['what', 'walks the typed AST directly, no LLVM']]))];
}

const VIEWS = { lex: viewLex, parse: viewParse, sema: viewSema, irgen: viewIrgen, opt: viewOpt, backend: viewBackend, run: viewRun };

function viewStage(run, st) {
  const res = run.stages[st.id];
  const head = stageHead(run, st);
  if (res.status === 'ok') return [head, VIEWS[st.id](run, res)].flat();
  // A run that compiled but was killed still has both output windows to show.
  if (res.status === 'error') return [head, st.id === 'run' && res.output ? viewRun(run, res) : errorView(run, res)].flat();
  if (res.status === 'running') return [head, notice(h('span', {}, h('span', { class: 'spin' }), ` running ${cmdFor(st.id, run)} …`))];
  if (res.status === 'skipped') {
    const f = failedStage(run);
    return [head, notice(`skipped: stage ${byId[f].n} (${byId[f].name}) failed, so nothing reached this one.`,
      h('div', { class: 'row' }, h('button', { type: 'button', onclick: () => go(f) }, `◂ see the ${byId[f].name} error`)))];
  }
  return [head, notice(
    'this stage has not run yet for the current compile.',
    h('div', { class: 'row' },
      h('button', { type: 'button', disabled: state.busy, onclick: () => advance(run, 'step') }, 'step ▸|'),
      h('button', { type: 'button', class: 'primary', disabled: state.busy, onclick: () => advance(run, 'all') }, 'run the rest ▸▸')))];
}

// ------------------------------------------------------------------ render

let lastViewKey = '';

function renderView(force = false) {
  const run = currentRun();
  const main = $('#view');
  if (state.stage === 'source' || !run) {
    if (!force && lastViewKey === 'source' && main.contains(editor?.root)) {
      renderEditorChrome();
      return;
    }
    lastViewKey = 'source';
    main.replaceChildren(...viewSource());
    return;
  }
  const res = run.stages[state.stage];
  const key = [state.stage, run.created, res.status, state.busy, state.optAfter, state.showDiff, state.showDirectives].join('|');
  if (!force && key === lastViewKey) return;
  lastViewKey = key;
  main.replaceChildren(...viewStage(run, byId[state.stage]));
}

function renderPipeline() {
  const run = currentRun();
  const nav = $('#pipeline');
  nav.replaceChildren(...STAGES.map((st) => {
    let status;
    let glyph;
    let meta;
    if (st.id === 'source') {
      status = 'ok';
      glyph = '›';
      meta = run && run.source !== state.draft ? 'editor · changed' : 'editor';
    } else if (!run) {
      status = 'pending';
      glyph = '·';
      meta = '—';
    } else {
      const res = run.stages[st.id];
      status = res.status;
      glyph = { ok: '✓', error: '✗', skipped: '–', pending: '·', running: h('span', { class: 'spin' }) }[status];
      meta = { ok: `${fmtMs(res.ms)}${res.cached ? ' · cached' : ''}`, error: 'failed', skipped: 'skipped', pending: 'pending', running: 'running' }[status];
    }
    return h('button', {
      type: 'button',
      class: `step ${status} ${state.stage === st.id ? 'viewing' : ''}`,
      disabled: st.id !== 'source' && !run,
      'aria-current': state.stage === st.id ? 'step' : null,
      onclick: () => {
        state.follow = false;
        go(st.id);
      },
    }, h('span', { class: 'name' }, h('span', { class: 'glyph' }, glyph), ` ${st.n} ${st.name}`), h('span', { class: 'meta' }, meta));
  }));
  // On narrow screens the bar scrolls sideways: keep the active stage in view.
  const active = nav.querySelector('.step.running') || nav.querySelector('.step.viewing');
  if (active && nav.scrollWidth > nav.clientWidth) {
    const left = active.offsetLeft - nav.offsetLeft;
    if (left < nav.scrollLeft || left + active.offsetWidth > nav.scrollLeft + nav.clientWidth) {
      nav.scrollLeft = Math.max(0, left - (nav.clientWidth - active.offsetWidth) / 2);
    }
  }
}

function renderStatus() {
  const run = currentRun();
  const bar = $('#status');
  const onSource = state.stage === 'source' || !run;
  const res = !onSource ? run.stages[state.stage] : null;
  const mode = state.busy ? 'RUN' : res?.status === 'error' ? 'ERR' : onSource ? 'EDIT' : 'VIEW';
  const where = onSource
    ? `editor · ${draftLabel()} · ${state.draft.split('\n').length} lines · next compile at -O${state.opt}`
    : `compiled ${run.name} · -O${run.opt} · ${clock(run.created)}`;
  let stageText = '';
  if (!onSource) {
    const st = byId[state.stage];
    stageText = `[${st.n}/7] ${st.name} · ${res.status}${res.ms != null ? ` · ${fmtMs(res.ms)}` : ''}${res.cached ? ' · cached' : ''}`;
  } else if (run) {
    const done = PIPE.filter((s) => run.stages[s.id].status === 'ok').length;
    stageText = `last compile: ${done}/7 stages ok`;
  }
  bar.replaceChildren(
    h('span', { class: `mode ${state.busy ? 'busy' : mode === 'ERR' ? 'error' : ''}` }, mode),
    h('span', {}, where),
    stageText ? h('span', {}, stageText) : '',
    state.flash ? h('span', { class: 'warn' }, state.flash) : '',
    h('span', { class: 'keys' },
      h('kbd', {}, 'ctrl+⏎'), ' run  ', h('kbd', {}, 'ctrl+⇧+⏎'), ' step  ', h('kbd', {}, '← →'), ' stages  ', h('kbd', {}, 'e'), ' editor'));
}

function renderTopbar() {
  for (const b of document.querySelectorAll('[data-opt]')) b.classList.toggle('on', Number(b.dataset.opt) === state.opt);
  $('#btn-run').disabled = state.busy;
  $('#btn-step').disabled = state.busy;
  const t = $('#target');
  if (!state.info) t.textContent = '';
  else if (!state.info.compiler) t.innerHTML = '<span class="err">ndlangc not found: build it, or set NDLANGC</span>';
  else t.textContent = `ndlangc · ${state.info.target || 'no LLVM backend'}`;
}

function render(forceView = false) {
  renderTopbar();
  renderPipeline();
  renderView(forceView);
  renderStatus();
}

// -------------------------------------------------------------------- boot

for (const b of document.querySelectorAll('[data-opt]')) {
  b.addEventListener('click', () => {
    state.opt = Number(b.dataset.opt);
    store.set('ndlang.opt', state.opt);
    render();
  });
}
$('#btn-run').addEventListener('click', () => compile('all'));
$('#btn-step').addEventListener('click', () => compile('step'));

document.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    compile(e.shiftKey ? 'step' : 'all');
    return;
  }
  const typing = e.target instanceof Element && e.target.closest('textarea, input, select');
  if (typing || e.altKey || e.ctrlKey || e.metaKey) return;
  if (e.key === 'ArrowLeft' || e.key === 'h') stepView(-1);
  else if (e.key === 'ArrowRight' || e.key === 'l') stepView(1);
  else if (e.key === 'e') {
    e.preventDefault();
    go('source');
    editor?.textarea.focus();
  } else return;
  e.preventDefault();
});

window.addEventListener('popstate', route);

async function boot() {
  const [info, examples] = await Promise.all([
    fetch('api/info').then((r) => r.json()).catch(() => null),
    fetch('api/examples').then((r) => r.json()).catch(() => null),
  ]);
  state.info = info;
  state.examples = examples;
  // First visit: start from the slide example rather than the template.
  const first = examples?.examples?.find((p) => p.name === '02_dot_product') || examples?.examples?.[0];
  if (firstVisit && first && state.draft === TEMPLATE) {
    state.draft = first.source;
    state.draftName = first.name;
    state.draftBase = first.source;
    saveDraft();
    editor?.setValue(state.draft);
  }
  if (!info) flash('cannot reach the explorer server');
  lastViewKey = '';
  route();
}

const firstVisit = state.draft == null;
if (firstVisit) {
  state.draft = TEMPLATE;
  state.draftName = 'untitled';
  state.draftBase = TEMPLATE;
}
route();
boot();
