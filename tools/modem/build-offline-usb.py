#!/usr/bin/env python3
"""Build a RAM-only offline image; never operates the phone."""
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
name = 'offline-usb' + ('-' + tag if tag else '')
kernel = repo / 'research/xnu-modem/dxb/fakeroot/System/Library/Kernels/kernel.development.sweet'
text = (root / 'next-fix/boot/build.sh').read_text()
match = re.search(r"^args='(.*)'$", text, re.M)
assert match
args = [x for x in match[1].split() if x.split('=')[0] not in
        ('keepsyms', 'sweetkeys_ms', 'sweettouchirq', 'sweetsyserr', 'sweetthermal', 'sweetmodem', 'sweetcpufreq_little', 'sweetfb_hz', 'sweetfetchsync', 'sweetcpufreq', 'debug', '-noprogress', 'sweetgpufast')]
args += ['sweetthermal=3', 'sweetfetchsync=1', 'sweetsb=1', 'sweetfb_ast_hw=1',
         'sweetfb_keep=1', 'sweetfb_astwait=1', 'sweettu=sysmem,nocb,nolrz',
         'sweetdeadman=600', 'sweetselftest=1', 'sweetmpss=1', 'sweetmglink=1', 'sweetmrmtfs=1', 'sweetmpdm=1', 'sweetmstop=1', 'sweetmho=1', 'sweetmbw=1', 'sweetusb=4']
cmdline = ' '.join(args)
assert len(cmdline.encode()) <= 607
assert 'sweetfbarm=1' in args
assert len({x.split('=')[0] for x in args}) == len(args), 'duplicate boot argument'
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
print('offline image ready; boot arguments:',len(cmdline.encode()),'bytes')
