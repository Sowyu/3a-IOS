#!/usr/bin/env python3
"""2152 v3 scope: nothing for panel modes. The patch must not touch the panel driver or call it, and the framebuffer must
keep sweetpwrfast off whenever panel commands are on (fPanel). Also mutates its inputs to show both rules are enforced.
Run: python3 -I patch_scope_check.py [patch] [framebuffer.cpp]"""
import pathlib, re, sys
h = pathlib.Path(__file__).resolve().parent
patch = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else h.parent / 'patches/2152-asteroids-fast-power.patch').read_text()
fb = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else h.parent / 'work/p4/iokit/Kernel/arm/SweetFramebuffer.cpp').read_text()


def check(patch, fb):
    fails = []
    files = re.findall(r'^\+\+\+ b/(\S+)', patch, re.M)
    if any('sweet_dsi' in f for f in files):
        fails.append('touches the panel driver')
    added = [l for l in patch.splitlines() if l.startswith('+') and not l.startswith('+++')]
    if any(re.search(r'sweet_panel_|dcs_|panel_plan|PANEL_(ON|DARK|ASLEEP)', l) for l in added):
        fails.append('adds a panel driver call or state')
    if not re.search(r'if \(mode != 0 && \(fPanel \|\|[^\n]*\)\) \{\n[^\n]*sweetpwrfast ignored', fb):
        fails.append('no guard turning sweetpwrfast off with panel commands')
    return files, fails


files, fails = check(patch, fb)
_, m1 = check(patch, fb.replace('(fPanel || ', '('))
_, m2 = check(patch + '\n+\t\tsweet_panel_post_power(false);\n', fb)
if not m1 or not m2:
    fails.append('a mutation was not caught (guard removed: %s, panel call added: %s)' % (bool(m1), bool(m2)))
print('files:', ', '.join(files))
print('mutations: guard removed %s, panel call added %s' % ('caught' if m1 else 'NOT CAUGHT', 'caught' if m2 else 'NOT CAUGHT'))
print('patch_scope_check: ' + ('ok' if not fails else 'FAILED: ' + '; '.join(fails)))
sys.exit(1 if fails else 0)
