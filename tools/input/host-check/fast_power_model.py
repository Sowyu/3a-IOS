#!/usr/bin/env python3
"""2152 regression check. Builds fast_power_model.c twice: OLD with v1's SweetFbScale.h (work-v1/s3), NEW with v2's
(work/s3) plus panel_plan cut from v2's sweet_dsi_asteroids.c. Every audit scenario must fail on OLD and pass on NEW, and
NEW's random walk must hold the invariants. Run: python3 -I fast_power_model.py [fuzz runs]"""
import os, pathlib, re, subprocess, sys, tempfile
h = pathlib.Path(__file__).resolve().parent
old_hdr = h.parent / 'work-v1/s3/iokit/Kernel/arm'
new_hdr = pathlib.Path(os.environ.get('FPM_HDR', h.parent / 'work/s3/iokit/Kernel/arm'))   # overrides for mutation tests
drv = pathlib.Path(os.environ.get('FPM_DRV', h.parent / 'work/s3/bsd/dev/sweet_dsi_asteroids.c')).read_text()
defs = ''.join(re.findall(r'^#define PANEL_(?:ON|DARK|ASLEEP)\s.*\n', drv, re.M))
plan = re.search(r'\nstatic uint8_t\npanel_plan\(.*?\n}\n', drv, re.S).group(0)
assert defs.count('#define') == 3 and plan
# name: (what to read, old must be, new must be) -- "old must" reproduces the audit's failure, "new must" is the fix
expect = {
    'f1-panel-gpu-recovery': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2600),
    'f1-pixel-gpu-recovery': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2600),
    'f1-dcs-restore-fails': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2600),
    'f1-dcs-restore-fails-29': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2600),
    'f1-dcs-unwake-fails-10': ('awake_off_max_ms', lambda v: v > 5000, lambda v: v <= 2600),
    'f2-wake-press-darkened': ('dark_on_max_ms', lambda v: v >= 1500, lambda v: v < 300),
    'f2-lock-press-woken': ('lit_off_max_ms', lambda v: v >= 1000, lambda v: v < 100),
    'f6-touch-cancels-wake': ('awake_after_touch_ms', lambda v: v >= 1000, lambda v: v < 150),
    'f7-flip-while-asleep': ('flips_asleep', lambda v: v >= 1, lambda v: v == 0),
}
def build_run(d, flag, hdr, args=()):
    exe = os.path.join(d, flag)
    subprocess.run(['cc', '-O1', '-Wall', '-Wno-unused-function', '-D' + flag, '-I', str(hdr), '-I', d, '-o', exe,
                    str(h / 'fast_power_model.c')], check=True)
    out = subprocess.run([exe, *args], check=True, text=True, stdout=subprocess.PIPE).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if p and p[0] in expect:
            res[p[0]] = {k: int(v) for k, v in (x.split('=') for x in p[1:])}
    return out, res
fails = 0
with tempfile.TemporaryDirectory(dir=h) as d:
    open(os.path.join(d, 'panel_plan.inc'), 'w').write('#include <stdint.h>\n' + defs + plan)
    old_out, old = build_run(d, 'OLD', old_hdr)
    new_out, new = build_run(d, 'NEW', new_hdr, sys.argv[1:2])
for name, (key, old_bad, new_ok) in expect.items():
    o, n = old[name][key], new[name][key]
    ok_old, ok_new = old_bad(o), new_ok(n)
    leak = new[name]['leak_max_ms']
    print('%-26s %-22s v1 %6d %s | v2 %6d %s | v2 leak %d ms' % (name, key, o, 'reproduces' if ok_old else 'DOES NOT REPRODUCE',
                                                               n, 'fixed' if ok_new else 'STILL FAILS', leak))
    fails += (not ok_old) + (not ok_new) + (leak > 40)
# panel_plan: every transition, its command, the state it reaches and the spacing the panel needs after the last command
table = r"""
#include <stdio.h>
int main(void) {
	struct { int phys, target; unsigned char last, cmd; int next; unsigned gap; } t[] = {
		{ PANEL_ON, PANEL_ON, 0, 0, PANEL_ON, 0 },
		{ PANEL_ON, PANEL_DARK, 0x29, 0x28, PANEL_DARK, 10 },
		{ PANEL_ON, PANEL_ASLEEP, 0, 0x28, PANEL_DARK, 0 },
		{ PANEL_DARK, PANEL_ASLEEP, 0x28, 0x10, PANEL_ASLEEP, 120 },
		{ PANEL_DARK, PANEL_ON, 0x28, 0x29, PANEL_ON, 17 },
		{ PANEL_DARK, PANEL_ON, 0x11, 0x29, PANEL_ON, 80 },
		{ PANEL_DARK, PANEL_ASLEEP, 0x11, 0x10, PANEL_ASLEEP, 80 },
		{ PANEL_ASLEEP, PANEL_ON, 0x10, 0x11, PANEL_DARK, 100 },
		{ PANEL_ASLEEP, PANEL_DARK, 0x10, 0x11, PANEL_DARK, 100 },
		{ PANEL_ASLEEP, PANEL_ASLEEP, 0x10, 0, PANEL_ASLEEP, 0 },
	};
	int bad = 0;
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		int next; unsigned gap;
		unsigned char c = panel_plan(t[i].phys, t[i].target, t[i].last, &next, &gap);
		if (c != t[i].cmd || next != t[i].next || gap != t[i].gap) {
			printf("panel_plan row %u: %02x next %d gap %u, want %02x %d %u\n", i, c, next, gap, t[i].cmd, t[i].next, t[i].gap);
			bad++;
		}
	}
	/* walking from any state to any target reaches it in at most three commands */
	for (int p = 0; p < 3; p++) for (int g = 0; g < 3; g++) {
		int s = p, n = 0; unsigned char last = 0;
		while (s != g && n < 4) { int nx; unsigned gp; last = panel_plan(s, g, last, &nx, &gp); s = nx; n++; }
		if (s != g || n > 3) { printf("panel_plan %d -> %d did not arrive\n", p, g); bad++; }
	}
	printf(bad ? "panel_plan: %d FAILED\n" : "panel_plan: ok\n", bad);
	return bad != 0;
}
"""
with tempfile.TemporaryDirectory(dir=h) as d:
    c = os.path.join(d, 't.c')
    open(c, 'w').write('#include <stdint.h>\n' + defs + plan + table)
    subprocess.run(['cc', '-O1', '-Wall', '-o', os.path.join(d, 't'), c], check=True)
    r = subprocess.run([os.path.join(d, 't')], text=True, stdout=subprocess.PIPE)
    print(r.stdout.strip())
    fails += r.returncode != 0
fuzz = [l for l in new_out.splitlines() if l.startswith('fuzz')]
print('\n'.join(fuzz))
if not fuzz or 'violations=0' not in fuzz[-1]:
    fails += 1
print('fast_power_model: %s' % ('ok' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
