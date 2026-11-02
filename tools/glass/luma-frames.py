#!/usr/bin/env python3
"""Adaptive glass check on self-test frames (analysis/glass-flat.md). For each run directory with frames/, reads the top
glass buttons and the status bar clock in Calculator (black app: the buttons must be dark) and Notes (white app: buttons
light, clock dark). Only frames from nohw runs are usable: hw runs' screenshots are scrambled.

  plans/glass/venv/bin/python -I plans/glass/test/luma-frames.py ios-asteroids/logs/ast97-*
"""
import sys, pathlib
import numpy as np
from PIL import Image


def mean(a, fx, fy, r=3):
    h, w, _ = a.shape
    x, y = int(fx * w), int(fy * h)
    return int(a[y - r:y + r, x - r:x + r].mean())


def check(run):
    out, bad = [], 0
    for name, want in (('13-com.apple.calculator.check.png', 'dark'), ('17-notes.check.png', 'light')):
        f = run / 'frames' / name
        if not f.exists():
            out.append('  %s missing' % name)
            continue
        a = np.asarray(Image.open(f).convert('RGB')).astype(int)
        h, w, _ = a.shape
        left, right = mean(a, 0.06, 0.097), mean(a, 0.94, 0.097)
        clock = a[int(0.015 * h):int(0.04 * h), int(0.03 * w):int(0.15 * w)].min()
        ok = (left < 100 and right < 100) if want == 'dark' else (left > 200 and right > 200 and clock < 100)
        bad += not ok
        out.append('  %-36s buttons %3d %3d clock min %3d -> %s' % (name, left, right, clock, 'ok' if ok else 'WRONG (want %s glass)' % want))
    return bad, out


if __name__ == '__main__':
    status = 0
    for d in sys.argv[1:]:
        run = pathlib.Path(d)
        if not (run / 'frames').is_dir():
            continue
        if 'nohw' not in run.name and 'visual' not in run.name:
            print('%s skipped: not a nohw run, its screenshots are scrambled' % run.name)
            continue
        bad, lines = check(run)
        status |= bad > 0
        print(run.name)
        print('\n'.join(lines))
    sys.exit(status)
