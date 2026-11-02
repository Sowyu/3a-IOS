#!/usr/bin/env python3
"""Frame pacing on the panel, from a trace run's kernel tick ring ("T tick flag id", kernel 2140), last boot only. Read-only.
usage: pacing.py RUN...
A burst: a stretch where shown frames are at most 70 ms apart, with 10 or more shown frames. Gaps between shown frames are counted
in 60 Hz vsyncs from the tick times (1 = 16.7 ms, 2 = 33 ms, ...), so a 30 Hz tick (sweetfb_hz=30) reads as a steady 2.
Prints: shown fps, the gap distribution, the share of frames held 2+ ticks, the flip rate (how often the gap changes from one
frame to the next: 0 for any steady cadence, near 1 for 1-2-1-2), and the share of time spent in steady runs of 6+ equal gaps."""
import sys, collections
exec(open(__file__.rsplit('/', 1)[0] + '/lagtrace.py').read().replace('\nmain()\n', '\n'))

def bursts(T):
    shown = [t for t, (fl, _) in T if fl == 1]
    out, cur = [], shown[:1]
    for a, b in zip(shown, shown[1:]):
        if b - a <= 70000: cur.append(b)
        else:
            if len(cur) >= 10: out.append(cur)
            cur = [b]
    if len(cur) >= 10: out.append(cur)
    return out

def report(run):
    d = run if '/' in run else 'ios-asteroids/logs/' + run
    F, S, T = load(d + '/bgmtl.log')
    bs = bursts(T)
    gaps = [[max(1, round((b - a) / 16667)) for a, b in zip(x, x[1:])] for x in bs]
    allg = [g for x in gaps for g in x]
    if not allg:
        print('%-24s no bursts' % d.split('/')[-1]); return
    c = collections.Counter(min(g, 4) for g in allg)
    n = len(allg)
    flips = sum(1 for x in gaps for a, b in zip(x, x[1:]) if a != b)
    pairs = sum(max(len(x) - 1, 0) for x in gaps)
    steady = 0
    for x in gaps:
        i = 0
        while i < len(x):
            j = i
            while j < len(x) and x[j] == x[i]: j += 1
            if j - i >= 6: steady += sum(x[i:j])
            i = j
    ticks = sum(allg)
    print('%-24s bursts %3d  shown %5.1f fps | gaps 1:%4.1f%% 2:%4.1f%% 3:%4.1f%% 4+:%4.1f%% | held 2+ %4.1f%% | flip rate %.2f | steady %4.1f%% of time' % (
        d.split('/')[-1], len(bs), 60.0 * n / ticks, 100 * c[1] / n, 100 * c[2] / n, 100 * c[3] / n, 100 * c[4] / n,
        100 * (n - c[1]) / n, flips / pairs if pairs else 0, 100 * steady / ticks))

for r in sys.argv[1:]:
    report(r)
