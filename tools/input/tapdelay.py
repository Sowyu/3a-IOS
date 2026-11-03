#!/usr/bin/env python3
"""Tap delay from an owner touch run: kernel touch lines (2154: "SWEET TOUCH: touch N: ... down D up U", absolute us) against
the frame trace (bgmtl.log F and S lines, same clock). Read-only. Run with python3 -I.
usage: tapdelay.py RUNDIR      (ios-asteroids/logs/astNNN-note; the kernel log is RUNDIR.txt)
       tapdelay.py --check
For each touch, the first frame CoreAnimation starts after the finger lands and after it lifts, and when that frame latched on
the panel. A touch counts as "from idle" when no frame started in the 150 ms before it, so that first frame is the response."""
import sys, re, bisect, os, statistics as st
_here = __file__.rsplit('/', 1)[0]
_lt = next(p for p in (_here + '/../../lag/tools/lagtrace.py', _here + '/../lag/lagtrace.py') if os.path.exists(p))   # plans/, or 3a-IOS/tools/
exec(open(_lt).read().replace('\nmain()\n', '\n'))

TOUCH = re.compile(r'SWEET TOUCH: touch (\d+): (\d+) ms, .*?(\d+) events, .* down (\d+) up (\d+)')

def touches(text):
    ls = text.split('\n')
    i = max([n for n, l in enumerate(ls) if 'devices before registration' in l], default=0)
    out = []
    for l in ls[i:]:
        m = TOUCH.search(l)
        if m: out.append(dict(n=int(m[1]), ms=int(m[2]), events=int(m[3]), down=int(m[4]), up=int(m[5])))
    return out

def frames(F, S):
    """Command buffers joined to the swap that showed them, as lagtrace.py does."""
    bys = {}
    for f in F: bys.setdefault(f['sid'], []).append(f)
    for s in S:
        c = bys.get(s['sid'], [])
        k = bisect.bisect_right([f['c0'] for f in c], s['swap']) - 1
        if k >= 0 and s['swap'] - c[k]['c0'] < 100000:
            c[k]['S'] = s
    return sorted((f for f in F if 'S' in f), key=lambda f: f['init'])

def response(fr, starts, t):
    """The first shown frame started at or after t: (start - t, latch - t or None, frame)."""
    k = bisect.bisect_left(starts, t)
    if k == len(fr): return None
    f = fr[k]
    return f['init'] - t, (f['S']['latch'] - t) if f['S']['latch'] else None, f

def analyse(T, fr):
    starts = [f['init'] for f in fr]
    rows = []
    for t in T:
        k = bisect.bisect_left(starts, t['down'])
        idle = k == 0 or t['down'] - starts[k - 1] > 150000
        rows.append(dict(t, idle=idle, d=response(fr, starts, t['down']), u=response(fr, starts, t['up'])))
    return rows

def ms(x): return '%6.1f' % (x / 1000) if x is not None else '     -'

def report(rows):
    print('touch  len ms  events  idle | down->start  down->latch | up->start  up->latch | response frame: encode gpu-done->tick tick->latch')
    for r in rows:
        d, u = r['d'], r['u']
        f = d[2] if d else None
        brk = ('%5.1f %5.1f %5.1f' % ((f['c0'] - f['init']) / 1000, (f['S']['tick'] - f['done']) / 1000 if f['S']['tick'] else -1,
                                     (f['S']['latch'] - f['S']['tick']) / 1000 if f['S']['latch'] and f['S']['tick'] else -1)) if f else ''
        print('%5d %7d %7d  %4s | %s       %s     | %s     %s    | %s' % (r['n'], r['ms'], r['events'], 'yes' if r['idle'] else 'no',
              ms(d[0] if d else None), ms(d[1] if d else None), ms(u[0] if u else None), ms(u[1] if u else None), brk))
    for name, key in (('down', 'd'), ('up', 'u')):
        for part, j in (('start', 0), ('latch', 1)):
            xs = [r[key][j] / 1000 for r in rows if r['idle'] and r[key] and r[key][j] is not None and r[key][j] < 1000000]
            if xs: print('from idle, %s -> %s: n=%d  p50 %.1f  p90 %.1f  mean %.1f ms' % (name, part, len(xs), q(xs, .5), q(xs, .9), st.mean(xs)))

def check():
    f = lambda init, latch: dict(init=init, c0=init + 1000, done=init + 9000, sid=1, S=dict(tick=init + 16000, latch=latch))
    fr = [f(1000000, 1020000), f(2050000, 2080000), f(2070000, 2100000)]
    rows = analyse([dict(n=1, ms=80, events=3, down=2000000, up=2060000)], fr)
    r = rows[0]
    assert r['idle'] and r['d'][0] == 50000 and r['d'][1] == 80000, r
    assert r['u'][0] == 10000 and r['u'][1] == 40000, r
    assert not analyse([dict(n=2, ms=1, events=1, down=1100000, up=1100001)], fr)[0]['idle']
    assert touches('x\nSWEET TOUCH: touch 7: 92 ms, 25 reads (0 fallback), 24 IRQ edges (260 per s), 4 events, '
                   'noticed to sent 1062 us (mode 7) down 123 up 456\n')[0] == dict(n=7, ms=92, events=4, down=123, up=456)
    print('tapdelay check ok')

if __name__ == '__main__':
    if sys.argv[1:] == ['--check']:
        check(); sys.exit(0)
    d = sys.argv[1].rstrip('/')
    F, S, _ = load(d + '/bgmtl.log')
    T = touches(open(d + '.txt', errors='replace').read())
    print('touches %d, frames %d' % (len(T), len(F)))
    report(analyse(T, frames(F, S)))
