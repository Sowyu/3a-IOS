#!/usr/bin/env python3
"""Frame timeline from a lag trace run (bgmtl.log "F", "S", "T" lines, kernel 2140). Read-only. Run with python3 -I.
usage: lagtrace.py RUNDIR [--show N] [--from US]
F: init first commit holdok submitted gpudone handlersdone g0 g1 draws sid tid   (mach absolute us; g0/g1 raw GPU ticks)
S: id begin swap tick scan rel latch wcall wret sid fence                       (kernel, absolute us)
T: tick flag id                                                                  (1 took a frame, 2 frame waiting for GPU)"""
import sys, re, bisect, statistics as st

def load(path):
    ls = open(path, errors='replace').read().split('\n')
    i = max([n for n, l in enumerate(ls) if 'devices before registration' in l], default=0)
    F, S, T = [], {}, {}
    for l in ls[i:]:
        l = re.sub(r'^\[\d+\] ', '', l)
        p = l.split()
        if not p: continue
        if p[0] == 'F' and len(p) == 13:
            v = list(map(int, p[1:]))
            F.append(dict(init=v[0], first=v[1], c0=v[2], hw=v[3], c1=v[4], done=v[5], hend=v[6], g0=v[7], g1=v[8], draws=v[9], sid=v[10], tid=v[11]))
        elif p[0] == 'S' and len(p) == 12:
            v = list(map(int, p[1:]))
            S[v[0]] = dict(id=v[0], begin=v[1], swap=v[2], tick=v[3], scan=v[4], rel=v[5], latch=v[6], wcall=v[7], wret=v[8], sid=v[9], fence=v[10])
        elif p[0] == 'T' and len(p) == 4:
            T[int(p[1])] = (int(p[2]), int(p[3]))
    F.sort(key=lambda f: f['c0'])
    return F, sorted(S.values(), key=lambda s: s['swap']), sorted(T.items())

def q(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p * len(xs)))] if xs else float('nan')

def summary(name, xs):
    xs = [x / 1000 for x in xs]
    if not xs: return '%-34s n=0' % name
    return '%-34s n=%4d  p10 %6.1f  p50 %6.1f  p90 %6.1f  mean %6.1f ms' % (name, len(xs), q(xs, .1), q(xs, .5), q(xs, .9), st.mean(xs))

def main():
    run = sys.argv[1]
    show = int(sys.argv[sys.argv.index('--show') + 1]) if '--show' in sys.argv else 0
    F, S, T = load(run + '/bgmtl.log')
    print('F %d  S %d  T %d' % (len(F), len(S), len(T)))
    if not F: return
    # GPU ticks: fit raw GPU ticks to CPU us on frames with timestamps (assume 19.2 MHz, find offset)
    g = [f for f in F if f['g0'] and f['g1'] > f['g0']]
    off = None
    if g:
        # GPU end must precede the completion thread's done; take the minimum of done - g1/19.2 as the offset estimate
        off = min(f['done'] - f['g1'] / 19.2 for f in g)
        lat = [f['done'] - (f['g1'] / 19.2 + off) for f in g]
        print('gpu ts: %d frames, offset %.0f us; done - gpu end p50 %.2f ms (relative to the tightest frame)' % (len(g), off, q(lat, .5) / 1000))
        # sanity: GPU start after commit?
        bad = sum(1 for f in g if f['g0'] / 19.2 + off < f['c0'] - 500)
        print('gpu start before its own commit (clock check, should be ~0): %d of %d' % (bad, len(g)))
    for f in F:
        f['gs'] = f['g0'] / 19.2 + off if off is not None and f['g0'] else 0
        f['ge'] = f['g1'] / 19.2 + off if off is not None and f['g1'] else 0
    # join swaps: each swap to the newest command buffer with the same surface committed before it
    bys = {}
    for f in F: bys.setdefault(f['sid'], []).append(f)
    for s in S:
        c = bys.get(s['sid'], [])
        k = bisect.bisect_right([f['c0'] for f in c], s['swap']) - 1
        if k >= 0 and s['swap'] - c[k]['c0'] < 100000:
            c[k]['S'] = s
    ticks = [t for t, _ in T]
    # frames: command buffers that rendered into a swapped surface
    fr = [f for f in F if 'S' in f]
    print('frames joined to swaps: %d of %d command buffers; surfaces %s' % (len(fr), len(F), sorted({f['sid'] for f in fr})))
    # busy runs: consecutive frames less than 50 ms apart
    pairs = [(a, b) for a, b in zip(fr, fr[1:]) if b['init'] - a['init'] < 50000]
    print('busy pairs (frame starts < 50 ms apart): %d' % len(pairs))
    per = [b['init'] - a['init'] for a, b in pairs]
    print(summary('frame period (init to init)', per))
    a_ = lambda k1, k2: [b[k2] - a[k1] for a, b in pairs if a.get(k1) and b.get(k2)]
    print(summary('this frame: init -> first encoder', [f['first'] - f['init'] for f in fr if f['first']]))
    print(summary('this frame: first -> commit (encode)', [f['c0'] - f['first'] for f in fr if f['first']]))
    print(summary('this frame: commit -> hold ok', [f['hw'] - f['c0'] for f in fr]))
    print(summary('this frame: hold ok -> submitted', [f['c1'] - f['hw'] for f in fr if f['c1']]))
    print(summary('this frame: submitted -> swap', [f['S']['swap'] - f['c1'] for f in fr if f['c1']]))
    print(summary('this frame: SwapBegin -> init', [f['init'] - f['S']['begin'] for f in fr if f['S']['begin']]))
    print(summary('this frame: commit -> gpu start', [f['gs'] - f['c0'] for f in fr if f['gs']]))
    print(summary('this frame: gpu busy', [f['ge'] - f['gs'] for f in fr if f['gs']]))
    print(summary('this frame: gpu end -> done (chl)', [f['done'] - f['ge'] for f in fr if f['ge']]))
    print(summary('this frame: done -> handlers done', [f['hend'] - f['done'] for f in fr]))
    print(summary('this frame: swap -> tick taken', [f['S']['tick'] - f['S']['swap'] for f in fr if f['S']['tick']]))
    print(summary('this frame: done -> tick taken', [f['S']['tick'] - f['done'] for f in fr if f['S']['tick']]))
    print(summary('this frame: tick -> hold released', [f['S']['rel'] - f['S']['tick'] for f in fr if f['S']['rel']]))
    print(summary('this frame: tick -> latched', [f['S']['latch'] - f['S']['tick'] for f in fr if f['S']['latch']]))
    print('-- what the next frame start (init) follows: next.init - prev.EVENT (negative = next started before it)')
    for k in ('c1', 'gs', 'ge', 'done', 'hend'):
        xs = a_(k, 'init')
        neg = sum(1 for x in xs if x < 0)
        print(summary('prev %s' % k, xs), ' negative %d' % neg)
    for k in ('swap', 'tick', 'rel', 'latch'):
        xs = [b['init'] - a['S'][k] for a, b in pairs if a['S'][k]]
        neg = sum(1 for x in xs if x < 0)
        print(summary('prev swap.%s' % k, xs), ' negative %d' % neg)
    # vsync phase of frame starts: init minus the newest tick before it
    if ticks:
        ph = []
        for f in fr:
            k = bisect.bisect_right(ticks, f['init']) - 1
            if k >= 0: ph.append(f['init'] - ticks[k])
        print(summary('init - newest kernel tick before it', ph))
        # how many ticks pass between consecutive frame starts
        tk = []
        for a, b in pairs:
            tk.append(bisect.bisect_right(ticks, b['init']) - bisect.bisect_right(ticks, a['init']))
        from collections import Counter
        print('ticks between frame starts:', sorted(Counter(tk).items()))
    # overlap: does frame N+1 encode while frame N is on the GPU?
    ov = sum(1 for a, b in pairs if a['ge'] and b['first'] and b['first'] < a['ge'])
    print('next frame starts encoding before this GPU work ends: %d of %d pairs' % (ov, len(pairs)))
    flags = {}
    for t, (fl, i) in T: flags[fl] = flags.get(fl, 0) + 1
    print('tick flags', flags)
    if show:
        start = int(sys.argv[sys.argv.index('--from') + 1]) if '--from' in sys.argv else None
        # choose the busiest stretch: the first window with 8 frames within 300 ms
        if start is None:
            for i in range(len(fr) - 8):
                if fr[i + 7]['init'] - fr[i]['init'] < 300000 and fr[i]['draws'] > 30:
                    start = fr[i]['init']; break
        sel = [f for f in fr if f['init'] >= start][:show]
        b = sel[0]['init']
        r = lambda x: '%7.1f' % ((x - b) / 1000) if x else '      -'
        print('ms from first init | init  first commit holdok submit swap  gpuS  gpuE  done hend | tick  rel latch | draws sid')
        for f in sel:
            s = f['S']
            print(' '.join(r(x) for x in (f['init'], f['first'], f['c0'], f['hw'], f['c1'], s['swap'], f['gs'], f['ge'], f['done'], f['hend'], s['tick'], s['rel'], s['latch'])), f['draws'], f['sid'])
        tl = [t for t in ticks if b - 20000 <= t <= sel[-1]['init'] + 40000]
        print('ticks:', ' '.join('%.1f/%d' % ((t - b) / 1000, T[[x for x, _ in T].index(t)][1][0]) for t in tl))

main()
