// Line-at-a-time syntax highlighters for every kind of text the explorer
// shows. Each takes one raw line and returns escaped HTML.

export function esc(s) {
  return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

// Wraps every named-group match of `re` in <span class="t-group">.
function tokenize(line, re) {
  let out = '';
  let last = 0;
  re.lastIndex = 0;
  for (const m of line.matchAll(re)) {
    if (!m[0]) continue;
    out += esc(line.slice(last, m.index));
    const group = Object.keys(m.groups).find((k) => m.groups[k] !== undefined);
    out += `<span class="t-${group}">${esc(m[0])}</span>`;
    last = m.index + m[0].length;
  }
  return out + esc(line.slice(last));
}

const NDL = new RegExp([
  /(?<com>\/\/.*$)/.source,
  /(?<num>\b\d+(?:\.\d+(?:[eE][+-]?\d+)?)?\b)/.source,
  /(?<kw>\b(?:func|let|return|if|else|for|in)\b)/.source,
  /(?<ty>\b(?:int|float|bool|vec)\b)/.source,
  /(?<lit>\b(?:true|false)\b)/.source,
  /(?<fn>\b[A-Za-z_]\w*(?=\s*\())/.source,
  /(?<op>->|\.\.|&&|\|\||[=!<>]=?|[-+*\/])/.source,
].join('|'), 'g');

const IR = new RegExp([
  /(?<com>;.*$)/.source,
  /(?<str>c?"(?:[^"\\]|\\.)*")/.source,
  /(?<lbl>^[\w.$-]+:)/.source,
  /(?<glb>@[\w.$-]+)/.source,
  /(?<loc>%[\w.$-]+)/.source,
  /(?<ty><\d+ x \w+>|\b(?:i1|i8|i32|i64|float|double|void|ptr|label)\b)/.source,
  /(?<kw>\b(?:define|declare|ret|br|call|tail|alloca|load|store|phi|icmp|fcmp|add|sub|mul|sdiv|fadd|fsub|fmul|fdiv|fneg|sitofp|fpext|insertelement|extractelement|shufflevector|select|xor|or|and|unreachable|getelementptr|switch|zext|sext|trunc)\b)/.source,
  /(?<attr>\b(?:private|unnamed_addr|constant|global|target|datalayout|triple|source_filename|attributes|align|nuw|nsw|disjoint|reassoc|noundef|nonnull|dereferenceable|local_unnamed_addr|poison|zeroinitializer|undef|slt|sgt|sle|sge|eq|ne|olt|ogt|ole|oge|oeq|une)\b|#\d+)/.source,
  /(?<num>\b-?\d+(?:\.\d+(?:e[+-]?\d+)?)?\b)/.source,
].join('|'), 'g');

const ASM = new RegExp([
  /(?<com>#.*$)/.source,
  /(?<lbl>^[\w.$]+:)/.source,
  /(?<dir>^\s+\.\w+.*$)/.source,
  /(?<kw>^\s+[a-z][a-z0-9.]*)/.source,
  /(?<loc>%\w+)/.source,
  /(?<num>\$-?[\w.]+|\b-?\d+\b)/.source,
].join('|'), 'g');

const AST = new RegExp([
  /(?<node>^\s*[A-Z][A-Za-z]*)/.source,
  /(?<ty>: (?:int|float|bool|void|vec<\d+>)(?=\s*$))/.source,
  /(?<attr>\[builtin\]|\(int -> float\))/.source,
  /(?<num>\b\d+(?:\.\d+)?\b)/.source,
].join('|'), 'g');

export const hlNdl = (line) => tokenize(line, NDL);
export const hlIr = (line) => tokenize(line, IR);
export const hlAsm = (line) => tokenize(line, ASM);
export const hlAst = (line) => tokenize(line, AST);
export const hlPlain = (line) => esc(line);
