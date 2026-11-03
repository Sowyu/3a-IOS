#!/usr/bin/env python3
"""Build a RAM-only data session image; never operates the phone."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
os.environ["TMPDIR"] = "/tmp"
root = Path(__file__).resolve().parents[3]
repo = root / 'ios-asteroids'
out = root / 'plans/modem/out'
out.mkdir(exist_ok=True)
assert len(sys.argv) <= 2, 'optional numeric image tag only'
tag = sys.argv[1] if len(sys.argv) == 2 else ''
assert not tag or re.fullmatch(r'[0-9]+', tag)
name = 'data-usb' + ('-' + tag if tag else '')
kernel = repo / 'research/xnu-modem/dxb/fakeroot/System/Library/Kernels/kernel.development.sweet'
cmdline = (root / 'plans/modem/data-draft/data-session-args.txt').read_text().strip()
args = cmdline.split()
assert len(cmdline.encode()) <= 607
assert len({x.split('=')[0] for x in args}) == len(args), 'duplicate boot argument'
required = ['cpus=8', 'sweetpanel=0', 'sweetfb_noconsole', 'sweetfbarm=1',
    'sweetdeadman=900', 'sweetselftest=1', 'sweetmpss=1', 'sweetmglink=1',
    'sweetmrmtfs=1', 'sweetmpdm=1', 'sweetmstop=1', 'sweetmho=1',
    'sweetusb=4', 'sweetmuim=1', 'sweetmready=1', 'sweetmsession=1',
    'sweetdata=1', 'sweetdataonline=1', 'sweetipa=1']
assert all(x in args for x in required), 'missing data-session guard'
assert not any(x.startswith('sweetqmiraw') for x in args), 'raw exports forbidden in first session'
# Hardware stages and final image still need a separate exact-artifact Sol GO.
(out / (name + '-args.txt')).write_text(cmdline + '\n')
shim = out / (name + '-shim')
if shim.exists():
    subprocess.run(['/usr/bin/trash', str(shim)], check=True)
shutil.copytree(root / 'ios-sweet/asteroids/shim', shim)
payload = out / (name + '.payload')
subprocess.run(['python3','-I',str(repo / 'tools/mkpayload.py'),str(kernel),
    str(root / 'ux-fix/ux-420pt-2x-840x1860.adt'),str(payload),'--cmdline',cmdline],check=True)
env = dict(os.environ, ASTEROIDS_DTB=str(repo / 'research/asteroids-hw/live.dtb'),
    MKBOOTIMG=str(root / 'ref/tools/mkbootimg.py'))
subprocess.run(['bash',str(shim / 'build.sh'),str(payload),str(out / (name + '-boot.img'))],env=env,check=True)
print('data image ready, not boot approved; boot arguments:',len(cmdline.encode()),'bytes')
