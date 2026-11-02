#!/usr/bin/env python3
"""Frames shown on the panel per second while animating, from the kernel log alone (works without the trace build). Read-only.
usage: shownfps.py RUN...  (ios-asteroids/logs/RUN.txt). Last boot only. An animating second: stats window with swaps_s >= 25.
Prints the mean copy_s (frames copied to the panel) over those seconds, their count, and the mean swaps_s."""
import re, sys, statistics as st
ST = re.compile(r'up_s=(\d+) .*?swaps_s=([\d.]+) .*?copy_s=([\d.]+)')
for run in sys.argv[1:]:
    p = run if run.endswith('.txt') else 'ios-asteroids/logs/%s.txt' % run
    t = open(p, errors='replace').read()
    i = t.rfind('SWEET FB: AppleCLCD')   # the last boot's framebuffer start
    rows = [(int(a), float(b), float(c)) for a, b, c in ST.findall(t[i:])]
    an = [r for r in rows if r[1] >= 25]
    if not an:
        print(run, 'no animating seconds'); continue
    print('%-26s shown %5.1f fps over %3d animating s (swaps %5.1f/s); p25 %4.1f p75 %4.1f' % (
        run, st.mean(r[2] for r in an), len(an), st.mean(r[1] for r in an), st.quantiles([r[2] for r in an], n=4)[0],
        st.quantiles([r[2] for r in an], n=4)[2]))
