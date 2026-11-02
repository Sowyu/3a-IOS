#!/usr/bin/env python3
"""Where a frame's time goes when the panel misses a vsync. Read-only; trace runs (kernel 2140/2141 + bgmtl trace), last boot.
usage: miss.py RUN...
Each shown frame inside an animation burst is joined to its swap (tick id) and to the command buffer that rendered into that
surface before the swap. A frame is "on time" when it was shown one 60 Hz vsync after the previous shown frame, "late" when 2+.
For the frame that arrives (the one closing the gap) the stages are, in ms:
  start   CoreAnimation's frame start (SwapBegin) after the previous shown frame's start
  ca      SwapBegin -> command buffer created (CoreAnimation's own work before encoding)
  enc     command buffer created -> commit (CoreAnimation encoding through bgmtl/chl)
  sub     commit -> submitted (bgmtl commit, chl flush, kernel submit)
  q       submitted -> GPU start (waiting behind earlier GPU work)
  gpu     GPU busy (its own timestamps)
  fence   GPU end -> completion thread saw the fence
  tick    GPU end -> the tick that took the frame
  copy    tick -> its hold released (copy) ; latch: tick -> latched
Also counted: frames CoreAnimation never started in a vsync (start gap), and SwapWait waits (2142)."""
import sys, bisect, statistics as st, collections
exec(open(__file__.rsplit('/', 1)[0] + '/lagtrace.py').read().replace('\nmain()\n', '\n'))

def med(xs):
    return st.median(xs) if xs else float('nan')

def report(run):
    d = run if '/' in run else 'ios-asteroids/logs/' + run
    F, S, T = load(d + '/bgmtl.log')
    g = [f for f in F if f['g1']]
    if not g:
        print(run, 'no GPU timestamps'); return
    off = min(f['done'] - f['g1'] / 19.2 for f in g)
    for f in F:
        f['gs'] = f['g0'] / 19.2 + off if f['g0'] else 0
        f['ge'] = f['g1'] / 19.2 + off if f['g1'] else 0
    F.sort(key=lambda f: f['c0'])
    bys = collections.defaultdict(list)
    for f in F: bys[f['sid']].append(f)
    sid_c0 = {k: [f['c0'] for f in v] for k, v in bys.items()}
    sbyid = {s['id']: s for s in S}
    for s in S:
        c = bys.get(s['sid'], []); k = bisect.bisect_right(sid_c0.get(s['sid'], []), s['swap']) - 1
        s['F'] = c[k] if k >= 0 and s['swap'] - c[k]['c0'] < 100000 else None
    shown = [(t, i) for t, (fl, i) in T if fl == 1 and i in sbyid and sbyid[i]['F']]
    rows = {'on': collections.defaultdict(list), 'late': collections.defaultdict(list)}
    n = collections.Counter()
    for (t0, i0), (t1, i1) in zip(shown, shown[1:]):
        gap = round((t1 - t0) / 16667)
        if gap < 1 or gap > 4: continue          # outside animation bursts
        a, b = sbyid[i0], sbyid[i1]
        fa, fb = a['F'], b['F']
        if not (b['begin'] and a['begin'] and fb['ge'] and fb['first']): continue
        key = 'on' if gap == 1 else 'late'
        if fb['draws'] < 30: continue           # light frames are always on time; look at the ones that can miss
        n[key] += 1
        r = rows[key]
        r['start'].append((b['begin'] - a['begin']) / 1e3)
        r['ca'].append((fb['init'] - b['begin']) / 1e3)
        r['enc'].append((fb['c0'] - fb['init']) / 1e3)
        r['sub'].append((fb['c1'] - fb['c0']) / 1e3 if fb['c1'] else float('nan'))
        r['q'].append((fb['gs'] - fb['c1']) / 1e3 if fb['c1'] else float('nan'))
        r['gpu'].append((fb['ge'] - fb['gs']) / 1e3)
        r['fence'].append((fb['done'] - fb['ge']) / 1e3)
        r['tick'].append((t1 - fb['ge']) / 1e3)
        r['copy'].append((b['rel'] - b['tick']) / 1e3 if b['rel'] and b['tick'] else float('nan'))
        r['latch'].append((b['latch'] - b['tick']) / 1e3 if b['latch'] and b['tick'] else float('nan'))
        r['swapwait'].append((a['wret'] - a['wcall']) / 1e3 if a['wret'] and a['wcall'] else 0)
        r['draws'].append(fb['draws'])
        r['start2gpuend'].append((fb['ge'] - b['begin']) / 1e3)
    name = d.split('/')[-1]
    print('== %s: frames of 30+ draws, on time %d, late %d (%.0f%% late)' % (name, n['on'], n['late'], 100 * n['late'] / max(1, n['on'] + n['late'])))
    keys = ['draws', 'start', 'ca', 'enc', 'sub', 'q', 'gpu', 'fence', 'tick', 'copy', 'latch', 'swapwait', 'start2gpuend']
    print('   median   ' + ' '.join('%7s' % k[:7] for k in keys))
    for key in ('on', 'late'):
        print('   %-8s ' % key + ' '.join('%7.1f' % med([x for x in rows[key][k] if x == x]) for k in keys))
    # which stage alone exceeds its on-time p90 most often among late frames
    if rows['late']['gpu']:
        over = collections.Counter()
        for k in ('start', 'ca', 'enc', 'sub', 'q', 'gpu', 'swapwait'):
            on = sorted(x for x in rows['on'][k] if x == x)
            if not on: continue
            p90 = on[int(0.9 * (len(on) - 1))]
            over[k] = sum(1 for x in rows['late'][k] if x == x and x > p90)
        print('   late frames with the stage above the on-time p90: ' + ', '.join('%s %d%%' % (k, 100 * v / n['late']) for k, v in over.most_common()))

for r in sys.argv[1:]:
    report(r)
