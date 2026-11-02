#!/usr/bin/env python3
"""2152 v3 regression check (pixel path). Builds fast_power_model.c as OLD (v2's header, work-v2/s3) and NEW (v3's header,
work/p4), checks each scenario's expectation, runs NEW's random walk, then the mutations: each switch takes one v3 fix out of
the glue mirror, and each header mutation edits a copy of v3's real sfs_fast_keep_next; every one must make a check fail.
Scratch files go in plans/input/work/tmp-model-* and are moved to the Trash with /usr/bin/trash at the end.
Run: python3 -I fast_power_model.py [fuzz runs]"""
import os, pathlib, subprocess, sys, tempfile
h = pathlib.Path(__file__).resolve().parent
old_hdr = h.parent / 'work-v2/s3/iokit/Kernel/arm'
new_hdr = pathlib.Path(os.environ.get('FPM_HDR', h.parent / 'work/p4/iokit/Kernel/arm'))
both = lambda name, ok: (name, ok, ok)
# name: (measure, v2 must, v3 must). v2 "must" reproduces the audit's failure; where v2 already passed, both must pass.
expect = {
    'f1-pixel-gpu-recovery': ('dark_on_max_ms', lambda v: v <= 2100, lambda v: v <= 2100),
    'f2-wake-press': ('dark_on_max_ms', lambda v: v < 300, lambda v: v < 300),
    'f4-blank-flip-fails-hw': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 100),
    'f4-restore-flip-fails-hw': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2100),
    'f4-restore-flip-fails': ('dark_on_max_ms', lambda v: v > 5000, lambda v: v <= 2100),
    'f4-restore-frozen': ('debt_dropped', lambda v: v >= 1, lambda v: v == 0),
    'f6-holds-keep': ('hold_age_max_ms', lambda v: v >= 1500, lambda v: v <= 60),
    'f6-holds': ('hold_age_max_ms', lambda v: v <= 60, lambda v: v <= 60),
}
mutations = [   # (what it undoes, -D switch or header edit)
    ('finding 4: a failed flip drops the debt', '-DMUT_CLEAR_ON_FAIL', None),
    ('finding 4: hardware-scale image not put in the boot buffer', '-DMUT_BOOT_BLACK', None),
    ('finding 4: a stop under a dark step (failed black flip) does not pay back at once', '-DMUT_NO_BLANK_FAIL', None),
    ('finding 6: the tick returns while dark, so holds stay', '-DMUT_EARLY_RETURN', None),
    ('finding 4 (header): stopped flip path counts as paid', None,
     ('	if (!boot_written) {\n		return SFS_KEEP_BOOT;\n	}', '	if (!boot_written) {\n		return SFS_KEEP_DONE;\n	}')),
    ('finding 4 (header): frozen scanout counts as paid', None,
     ('return frozen ? SFS_KEEP_WAIT : SFS_KEEP_DONE;', 'return SFS_KEEP_DONE;')),
]
tmp = pathlib.Path(tempfile.mkdtemp(prefix='tmp-model-', dir=h.parent / 'work'))

def build_run(flag, hdr, extra=(), args=()):
    exe = tmp / ('m' + str(abs(hash((flag, str(hdr), extra)))))
    subprocess.run(['cc', '-O1', '-Wall', '-Wno-unused-function', '-D' + flag, *extra, '-I', str(hdr), '-o', str(exe),
                    str(h / 'fast_power_model.c')], check=True)
    out = subprocess.run([str(exe), *args], check=True, text=True, stdout=subprocess.PIPE).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if p and p[0] in expect:
            res[p[0]] = {k: int(v) for k, v in (x.split('=') for x in p[1:])}
    return out, res

def failing(res):
    return [n for n, (k, _, ok) in expect.items() if not ok(res[n][k]) or res[n]['leak_max_ms'] > 40]

fails = 0
try:
    _, old = build_run('OLD', old_hdr)
    new_out, new = build_run('NEW', new_hdr, args=sys.argv[1:2] or ['3000'])
    for name, (key, old_ok, new_ok) in expect.items():
        o, n = old[name][key], new[name][key]
        a, b = old_ok(o), new_ok(n)
        print('%-26s %-16s v2 %6d %-18s v3 %6d %-12s v3 leak %d ms' % (name, key, o, 'as expected' if a else 'UNEXPECTED', n,
              'passes' if b else 'FAILS', new[name]['leak_max_ms']))
        fails += (not a) + (not b) + (new[name]['leak_max_ms'] > 40)
    fuzz = [l for l in new_out.splitlines() if l.startswith('fuzz')]
    print('\n'.join(fuzz))
    fails += not fuzz or 'violations=0' not in fuzz[-1]
    src = (new_hdr / 'SweetFbScale.h').read_text()
    for what, flag, edit in mutations:
        hdr, extra = new_hdr, ()
        if flag:
            extra = (flag,)
        else:
            assert src.count(edit[0]) == 1, edit[0]
            hdr = tmp / ('hdr' + str(abs(hash(edit[0]))))
            hdr.mkdir()
            (hdr / 'SweetFbScale.h').write_text(src.replace(edit[0], edit[1]))
        _, res = build_run('NEW', hdr, extra, ['0'])
        caught = failing(res)
        print('mutation %-60s %s' % (what, ('caught by ' + ', '.join(caught)) if caught else 'NOT CAUGHT'))
        fails += not caught
finally:
    subprocess.run(['/usr/bin/trash', str(tmp)], check=True)
print('fast_power_model: %s' % ('ok' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
