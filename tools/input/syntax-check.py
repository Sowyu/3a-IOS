#!/usr/bin/env python3
"""-fsyntax-only of every staged file in plans/input/work/STAGE with the kernel build's own flags (from the build's .o.json and
.cpo.json compile records). Quote includes that are not staged resolve from the file's real directory in the tree. Run with
python3 -I. usage: syntax-check.py [STAGE ...]   (default s1..s4 that exist). Writes work/syntax-STAGE.log."""
import json, os, pathlib, subprocess, sys
h = pathlib.Path(__file__).resolve().parent.parent
root = h.parent.parent
obj = root / 'ios-asteroids/research/xnu-local/dxb/build/xnu.obj/DEVELOPMENT_ARM64_SWEET'
stages = sys.argv[1:] or [s for s in ('s1', 's2', 's3', 's4') if (h / 'work' / s).is_dir()]
tmp = h / 'work/tmp'
tmp.mkdir(parents=True, exist_ok=True)
bad = 0
for stage in stages:
    log = []
    for src in sorted((h / 'work' / stage).rglob('*')):
        if src.suffix not in ('.c', '.cpp'):
            continue
        rec = next(obj.rglob(src.stem + ('.cpo.json' if src.suffix == '.cpp' else '.o.json')))
        d = json.loads(rec.read_text().strip().rstrip(','))
        args, out, i = d['arguments'], [], 0
        while i < len(args):
            a = args[i]
            i += 1
            if a in ('-o', '-MF', '-MT', '-MQ', '-MJ', '-serialize-diagnostics'):
                i += 1
                continue
            if a in ('-c', '-MD', '-MMD', '-MP'):
                continue
            out.append(str(src) if a == d['file'] else a)
        out += ['-fsyntax-only', '-I' + str(pathlib.Path(d['file']).parent)]
        p = subprocess.run(out, cwd=d['directory'], env=dict(os.environ, TMPDIR=str(tmp)), text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        mine = [l for l in p.stdout.splitlines() if str(src) in l and ('error' in l or 'warning' in l)]
        log.append('%s %s exit %d, %d diagnostics in the file\n%s' % (stage, src.relative_to(h / 'work' / stage), p.returncode,
                                                                      len(mine), p.stdout))
        print('%s %-40s exit %d  diagnostics in file %d' % (stage, src.name, p.returncode, len(mine)))
        for l in mine[:12]:
            print('   ', l)
        bad |= p.returncode != 0
    (h / 'work' / ('syntax-%s.log' % stage)).write_text('\n'.join(log))
sys.exit(1 if bad else 0)
