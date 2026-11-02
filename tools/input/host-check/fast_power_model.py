#!/usr/bin/env python3
"""2152 regression check (pixel path). Builds fast_power_model.c three times: v2 (work-v2/s3's header, -DOLD), v3 (work/p4,
-DNEW) and v4 (work/p5, -DNEW -DV4). Checks each scenario's expectation per version, runs v4's differential random walk, then
the mutations on v4: each switch takes one fix out of the glue mirror, each header edit changes a copy of the real
sfs_fast_keep_next; every one must make a check fail. Scratch goes in plans/input/work/tmp-model-* and to the Trash.
Run: python3 -I fast_power_model.py [fuzz runs]"""
import os, pathlib, subprocess, sys, tempfile
h = pathlib.Path(__file__).resolve().parent
hdr = {'v2': h.parent / 'work-v2/s3/iokit/Kernel/arm', 'v3': h.parent / 'work/p4/iokit/Kernel/arm',
       'v4': pathlib.Path(os.environ.get('FPM_HDR', h.parent / 'work/p5/iokit/Kernel/arm'))}
flags = {'v2': ['-DOLD'], 'v3': ['-DNEW'], 'v4': ['-DNEW', '-DV4']}
ok = lambda f: f
dark_le = lambda n: ('dark_on_max_ms', lambda v: v <= n)
# scenario: {version: (measure, test)}; None = not checked for that version
expect = {
    'f1-pixel-gpu-recovery': {v: dark_le(2100) for v in ('v2', 'v3', 'v4')},
    'f2-wake-press': {v: ('dark_on_max_ms', lambda x: x < 300) for v in ('v2', 'v3', 'v4')},
    'f4-blank-flip-fails-hw': {'v2': ('dark_on_max_ms', lambda x: x > 5000), 'v3': dark_le(100), 'v4': dark_le(100)},
    'f4-restore-flip-fails-hw': {'v2': ('dark_on_max_ms', lambda x: x > 5000), 'v3': dark_le(2100), 'v4': dark_le(2100)},
    'f4-restore-flip-fails': {'v2': ('dark_on_max_ms', lambda x: x > 5000), 'v3': dark_le(2100), 'v4': dark_le(2100)},
    'f4-restore-frozen': {'v2': ('debt_dropped', lambda x: x >= 1), 'v3': ('debt_dropped', lambda x: x == 0),
                          'v4': ('debt_dropped', lambda x: x == 0)},
    'f6-holds-keep': {'v2': ('hold_age_max_ms', lambda x: x >= 1500), 'v3': ('hold_age_max_ms', lambda x: x <= 60),
                      'v4': ('hold_age_max_ms', lambda x: x <= 60)},
    'f6-holds': {v: ('hold_age_max_ms', lambda x: x <= 60) for v in ('v2', 'v3', 'v4')},
    # audit 3 finding 2: v3 shows an incomplete image (reproduces), v4 never does and never enters a fallback while black
    'f2-abort-multilayer': {'v3': ('bad_shown_ms', lambda x: x > 1000), 'v4': ('bad_shown_ms', lambda x: x == 0)},
    'f2-epoch-reject': {'v3': ('bad_shown_ms', lambda x: x > 1000), 'v4': ('bad_shown_ms', lambda x: x == 0)},
}
v4_extra = {'f2-abort-multilayer': ('fallback_in_dark', lambda x: x == 0)}
mutations = [
    ('finding 4: a failed flip drops the debt', '-DMUT_CLEAR_ON_FAIL', None),
    ('finding 4: hardware-scale image not put in the boot buffer', '-DMUT_BOOT_BLACK', None),
    ('finding 4: a stop under a dark step does not pay at once', '-DMUT_NO_BLANK_FAIL', None),
    ('finding 6: the tick returns while dark, so holds stay', '-DMUT_EARLY_RETURN', None),
    ('audit 3 finding 2: default-surface record not refused while black', '-DMUT_NO_PREFLIGHT', None),
    ('audit 3 finding 2: stage counted complete before astFlip', '-DMUT_STAGE_VALID_EARLY', None),
    ('finding 4 (header): stopped flip path counts as paid', None,
     ('	if (!boot_written) {\n		return SFS_KEEP_BOOT;\n	}', '	if (!boot_written) {\n		return SFS_KEEP_DONE;\n	}')),
    ('finding 4 (header): frozen scanout counts as paid', None,
     ('return frozen ? SFS_KEEP_WAIT : SFS_KEEP_DONE;', 'return SFS_KEEP_DONE;')),
]
tmp = pathlib.Path(tempfile.mkdtemp(prefix='tmp-model-', dir=h.parent / 'work'))
n_exe = [0]

def build_run(fl, header, args=()):
    n_exe[0] += 1
    exe = tmp / ('m%d' % n_exe[0])
    subprocess.run(['cc', '-O1', '-Wall', '-Wno-unused-function', *fl, '-I', str(header), '-o', str(exe),
                    str(h / 'fast_power_model.c')], check=True)
    out = subprocess.run([str(exe), *args], check=True, text=True, stdout=subprocess.PIPE).stdout
    res = {}
    for line in out.splitlines():
        p = line.split()
        if p and p[0] in expect:
            res[p[0]] = {k: int(v) for k, v in (x.split('=') for x in p[1:])}
    return out, res

def v4_failing(res):
    bad = [n for n, e in expect.items() if not e['v4'][1](res[n][e['v4'][0]]) or res[n]['leak_max_ms'] > 40]
    bad += [n for n, (k, t) in v4_extra.items() if not t(res[n][k])]
    return bad

fails = 0
try:
    runs = sys.argv[1:2] or ['3000']
    out, res = {}, {}
    for v in ('v2', 'v3', 'v4'):
        out[v], res[v] = build_run(flags[v], hdr[v], runs if v == 'v4' else ['0'])
    for name, e in expect.items():
        cells = []
        for v in ('v2', 'v3', 'v4'):
            if v not in e:
                cells.append('%s %6s %-6s' % (v, '-', ''))
                continue
            key, test = e[v]
            val = res[v][name][key]
            good = test(val) and (v != 'v4' or res[v][name]['leak_max_ms'] <= 40)
            if v == 'v4' and name in v4_extra:
                good = good and v4_extra[name][1](res[v][name][v4_extra[name][0]])
            cells.append('%s %6d %-6s' % (v, val, 'ok' if good else 'WRONG'))
            fails += not good
        print('%-26s %-16s %s' % (name, e['v4'][0], ' | '.join(cells)))
    print('f2-abort-multilayer v4 fallback_in_dark=%d (v3 %d)' % (res['v4']['f2-abort-multilayer']['fallback_in_dark'],
                                                                 res['v3']['f2-abort-multilayer']['fallback_in_dark']))
    fuzz = [l for l in out['v4'].splitlines() if l.startswith('fuzz')]
    print('v4 ' + '\nv4 '.join(fuzz))
    fails += not fuzz or 'violations=0' not in fuzz[-1]
    src = (hdr['v4'] / 'SweetFbScale.h').read_text()
    for what, flag, edit in mutations:
        header, fl = hdr['v4'], flags['v4'] + ([flag] if flag else [])
        if edit:
            assert src.count(edit[0]) == 1, edit[0]
            header = tmp / ('hdr%d' % n_exe[0])
            header.mkdir()
            (header / 'SweetFbScale.h').write_text(src.replace(edit[0], edit[1]))
        _, r = build_run(fl, header, ['0'])
        caught = v4_failing(r)
        print('mutation %-66s %s' % (what, ('caught by ' + ', '.join(caught)) if caught else 'NOT CAUGHT'))
        fails += not caught
finally:
    subprocess.run(['/usr/bin/trash', str(tmp)], check=True)
print('fast_power_model: %s' % ('ok' if not fails else '%d FAILED' % fails))
sys.exit(1 if fails else 0)
