#!/usr/bin/env python3
"""-fsyntax-only of work/b/bsd/dev/sweet_usb.c with the combined kernel build's own flags (its sweet_usb.o.json compile
record). Quote includes not in the stage resolve from the real tree. Reads the combined tree, writes nothing there.
usage: python3 -I host/syntax-check.py [b|m]   b = after 2160+2161 (default), m = after 2160 only. Writes work/syntax-STAGE.log"""
import json, os, pathlib, subprocess, sys
here = pathlib.Path(__file__).resolve().parent.parent
root = here.parent.parent.parent
rec = root / 'ios-asteroids/research/xnu-combined/dxb/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT/sweet_usb.o.json'
stage = sys.argv[1] if len(sys.argv) > 1 else 'b'
src = here / 'work' / stage / 'bsd/dev/sweet_usb.c'
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
mine = [l for l in p.stdout.splitlines() if ('sweet_usb' in l) and ('error' in l or 'warning' in l)]
(here / ('work/syntax-%s.log' % stage)).write_text(p.stdout)
print(stage + ' sweet_usb.c exit %d, %d diagnostics in sweet_usb.c / sweet_usb_gadget.h' % (p.returncode, len(mine)))
for l in mine[:40]:
    print('   ', l)
sys.exit(1 if p.returncode else 0)
