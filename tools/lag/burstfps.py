#!/usr/bin/env python3
"""Frame rate inside animation bursts, from a lag trace run's "S" swap lines (kernel 2140), last boot only. Read-only.
usage: burstfps.py RUN...   A burst: 10 or more swaps in a row with no gap over 50 ms. Reports time-weighted fps, the share of
frame intervals within one, two and three vsyncs, and the heavy-frame (80+ draws) period from the F lines."""
import sys, statistics as st
exec(open(__file__.rsplit('/', 1)[0] + '/lagtrace.py').read().replace('\nmain()\n', '\n'))
for run in sys.argv[1:]:
    d = run if '/' in run else 'ios-asteroids/logs/' + run
    F, S, T = load(d + '/bgmtl.log')
    sw = [s['swap'] for s in S]
    bursts, cur = [], [sw[0]] if sw else []
    for a, b in zip(sw, sw[1:]):
        if b - a <= 50000: cur.append(b)
        else:
            if len(cur) >= 10: bursts.append(cur)
            cur = [b]
    if len(cur) >= 10: bursts.append(cur)
    frames = sum(len(b) - 1 for b in bursts); dur = sum(b[-1] - b[0] for b in bursts)
    iv = [y - x for b in bursts for x, y in zip(b, b[1:])]
    share = lambda lim: 100 * sum(1 for x in iv if x <= lim) / len(iv) if iv else 0
    import bisect
    tt = [t for t, _ in T]; tf = [v[0] for _, v in T]
    shown = 0; span = 0
    for b in bursts:
        i, j = bisect.bisect_left(tt, b[0]), bisect.bisect_right(tt, b[-1] + 17000)
        if j - i < 2: continue
        shown += sum(1 for k in range(i, j) if tf[k] == 1); span += tt[j - 1] - tt[i]
    F.sort(key=lambda f: f['init'])
    hp = [b['init'] - a['init'] for a, b in zip(F, F[1:]) if a['draws'] >= 80 and b['init'] - a['init'] < 50000]
    print('%-22s shown %5.1f fps;' % (d.split('/')[-1], shown / (span / 1e6) if span else 0), end=' ')
    print('%-22s bursts %3d, %5.1f s: %5.1f swaps/s; intervals <=18 ms %4.1f%%, <=35 %4.1f%%, <=52 %4.1f%%; heavy period p50 %.1f ms' % (
        d.split('/')[-1], len(bursts), dur / 1e6, frames / (dur / 1e6) if dur else 0, share(18000), share(35000), share(52000),
        st.median(hp) / 1000 if hp else 0))
