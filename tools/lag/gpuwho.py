#!/usr/bin/env python3
"""Who else is on the GPU around backboardd's frames (kernel 2141 G/W lines in bgmtl.log). Read-only. python3 -I gpuwho.py RUNDIR
For each pair of consecutive heavy frames: the GPU events between the first frame's GPU end - 3 ms and the next frame's SwapBegin."""
import sys, re, bisect, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
run = sys.argv[1]
src = open(__file__.rsplit('/', 1)[0] + '/lagtrace.py').read().replace('\nmain()\n', '\n')
exec(src)
F, S, T = load(run + '/bgmtl.log')
ls = open(run + '/bgmtl.log', errors='replace').read().split('\n')
i0 = max([n for n, l in enumerate(ls) if 'devices before registration' in l], default=0)
bb = None
G = {}
for l in ls[i0:]:
    m = re.match(r'^(?:\[(\d+)\] )?([GW]) (\d+) (\d+) (\d+) (\d+) (\d+)$', l)
    if m:
        k = (m[2], int(m[3]), int(m[5]), int(m[6]))
        G[k] = (m[2], int(m[3]), int(m[4]), int(m[5]), int(m[6]), int(m[7]))
    m = re.match(r'^\[(\d+)\] ftrace: on', l)
    if m: bb = int(m[1])
G = sorted(G.values(), key=lambda g: g[1])
print('backboardd pid', bb, 'GPU events', len(G))
print('submissions by pid', collections.Counter(g[3] for g in G if g[0] == 'G').most_common(8))
print('waits by pid', collections.Counter(g[3] for g in G if g[0] == 'W').most_common(8))
off = min(f['done'] - f['g1'] / 19.2 for f in F if f['g1'])
for f in F: f['ge'] = f['g1'] / 19.2 + off; f['gs'] = f['g0'] / 19.2 + off
F.sort(key=lambda f: f['init'])
bys = {}
for f in F: bys.setdefault(f['sid'], []).append(f)
for s in S:
    c = bys.get(s['sid'], []); k = bisect.bisect_right([f['c0'] for f in c], s['swap']) - 1
    if k >= 0 and s['swap'] - c[k]['c0'] < 100000: c[k]['S'] = s
gt = [g[1] for g in G]
lo_t = gt[0] if gt else 0
pairs = [(a, b) for a, b in zip(F, F[1:]) if 'S' in a and 'S' in b and b['S']['begin'] and a['draws'] >= 80 and b['init'] - a['init'] < 60000 and a['ge'] > lo_t]
print('heavy pairs inside the GPU trace window', len(pairs))
# the newest W (any pid) returning before next SwapBegin, and its distance
cw = collections.Counter(); dist = []
other_sub = 0
for a, b in pairs:
    t1 = b['S']['begin']
    ws = [g for g in G if g[0] == 'W' and a['ge'] - 3000 <= g[1] <= t1]
    if ws:
        w = ws[-1]; cw[(w[3] == bb, w[5])] += 1; dist.append(t1 - w[1])
    if any(g[0] == 'G' and g[3] != bb and a['gs'] <= g[1] <= t1 for g in G): other_sub += 1
print('newest wait return before next SwapBegin: (is backboardd, flags)', cw.most_common())
dist.sort()
if dist: print('next SwapBegin - that wait return, us: p10 %d p50 %d p90 %d' % (dist[len(dist) // 10], dist[len(dist) // 2], dist[9 * len(dist) // 10]))
print('pairs with another process submitting during the frame: %d of %d' % (other_sub, len(pairs)))
if '--show' in sys.argv:
    k0 = len(pairs) // 2
    for a, b in pairs[k0:k0 + int(sys.argv[sys.argv.index("--show") + 1])]:
        base = a['init']
        print('--- frame: init 0, commit %.1f, gpu %.1f..%.1f, chl done %.1f, next SwapBegin %.1f' % ((a['c0'] - base) / 1e3, (a['gs'] - base) / 1e3, (a['ge'] - base) / 1e3, (a['done'] - base) / 1e3, (b['S']['begin'] - base) / 1e3))
        for g in G:
            if base <= g[1] <= b['S']['begin'] + 500:
                print('   %s t=%.1f%s pid %d%s fence %d arg %d' % (g[0], (g[1] - base) / 1e3, ' (from %.1f)' % ((g[2] - base) / 1e3) if g[2] else '', g[3], ' bb' if g[3] == bb else '', g[4], g[5]))
