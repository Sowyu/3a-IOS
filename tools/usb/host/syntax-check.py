#!/usr/bin/env python3
"""-fsyntax-only of work/b/bsd/dev/sweet_usb.c with the combined kernel build's own flags (its sweet_usb.o.json compile
record). Quote includes not in the stage resolve from the real tree. Reads the combined tree, writes nothing there.
usage: python3 -I host/syntax-check.py [STAGE [FILE]]   STAGE b, m (v1) or b2, m2 (v2); FILE default sweet_usb.c.
Writes work/syntax-STAGE[-FILE].log"""
import json, os, pathlib, subprocess, sys
here = pathlib.Path(__file__).resolve().parent.parent
root = here.parent.parent.parent
stage = sys.argv[1] if len(sys.argv) > 1 else 'b'
name = sys.argv[2] if len(sys.argv) > 2 else 'sweet_usb.c'
src = here / 'work' / stage / 'bsd/dev' / name
rec = root / 'ios-asteroids/research/xnu-combined/dxb/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT' / (src.stem + '.o.json')
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
tmp = here / 'work/tmp'
tmp.mkdir(parents=True, exist_ok=True)
extra = []
cmd = out + ['-fsyntax-only', '-I' + str(src.parent), '-I' + str(pathlib.Path(d['file']).parent)] + extra
p = subprocess.run(cmd, cwd=d['directory'], env=dict(os.environ, TMPDIR=str(tmp)), text=True,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
mine = [l for l in p.stdout.splitlines() if (src.stem in l or 'sweet_usb_gadget' in l) and ('error' in l or 'warning' in l)]
(here / ('work/syntax-%s%s.log' % (stage, '' if name == 'sweet_usb.c' else '-' + src.stem))).write_text(p.stdout)
print('%s %s exit %d, %d diagnostics in the file' % (stage, name, p.returncode, len(mine)))
for l in mine[:40]:
    print('   ', l)
sys.exit(1 if p.returncode else 0)
