// Line diff (longest common subsequence) and side-by-side alignment for the
// optimizer's before/after windows.

// Returns [['eq', a, b] | ['del', a] | ['add', b], ...].
export function diffLines(a, b) {
  const n = a.length;
  const m = b.length;
  let pre = 0;
  while (pre < n && pre < m && a[pre] === b[pre]) pre++;
  let suf = 0;
  while (suf < n - pre && suf < m - pre && a[n - 1 - suf] === b[m - 1 - suf]) suf++;

  const A = a.slice(pre, n - suf);
  const B = b.slice(pre, m - suf);
  const N = A.length;
  const M = B.length;
  const ops = [];
  for (let i = 0; i < pre; i++) ops.push(['eq', a[i], b[i]]);

  if (N * M > 4e6) {
    // Too big for the quadratic table; show it as one replaced block.
    for (const x of A) ops.push(['del', x]);
    for (const y of B) ops.push(['add', y]);
  } else {
    const W = M + 1;
    const dp = new Uint32Array((N + 1) * W);
    for (let i = N - 1; i >= 0; i--) {
      for (let j = M - 1; j >= 0; j--) {
        dp[i * W + j] = A[i] === B[j] ? dp[(i + 1) * W + j + 1] + 1 : Math.max(dp[(i + 1) * W + j], dp[i * W + j + 1]);
      }
    }
    let i = 0;
    let j = 0;
    while (i < N && j < M) {
      if (A[i] === B[j]) ops.push(['eq', A[i++], B[j++]]);
      else if (dp[(i + 1) * W + j] >= dp[i * W + j + 1]) ops.push(['del', A[i++]]);
      else ops.push(['add', B[j++]]);
    }
    while (i < N) ops.push(['del', A[i++]]);
    while (j < M) ops.push(['add', B[j++]]);
  }

  for (let k = 0; k < suf; k++) ops.push(['eq', a[n - suf + k], b[m - suf + k]]);
  return ops;
}

// Pairs deletions with insertions so both sides have the same number of rows:
// [{ l, r, kind: 'eq' | 'chg' | 'del' | 'add' }], l/r null for a gap.
export function alignRows(ops) {
  const rows = [];
  let dels = [];
  let adds = [];
  const flush = () => {
    const k = Math.max(dels.length, adds.length);
    for (let i = 0; i < k; i++) {
      const hasL = i < dels.length;
      const hasR = i < adds.length;
      rows.push({ l: hasL ? dels[i] : null, r: hasR ? adds[i] : null, kind: hasL && hasR ? 'chg' : hasL ? 'del' : 'add' });
    }
    dels = [];
    adds = [];
  };
  for (const [t, x, y] of ops) {
    if (t === 'eq') {
      flush();
      rows.push({ l: x, r: y, kind: 'eq' });
    } else if (t === 'del') {
      dels.push(x);
    } else {
      adds.push(x);
    }
  }
  flush();
  return rows;
}
