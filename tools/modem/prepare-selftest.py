#!/usr/bin/env python3
"""Prepare a fresh completion token and selftest. Does not operate a phone."""
from pathlib import Path
import argparse,subprocess,uuid
here=Path(__file__).resolve().parent
root=here.parents[2]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('stage',choices=['power-probe','offline','uim'])
a=ap.parse_args();name='modem-'+a.stage
s=(here/(name+'.sh')).read_text();assert s.count('@RUN_NONCE@')==1
nonce=uuid.uuid4().hex
out=root/'plans/modem/out';out.mkdir(parents=True,exist_ok=True)
noncefile=out/(name+'-nonce.txt');noncefile.write_text(nonce+'\n')
script=root/'plans/devloop/selftest/scripts'/(name+'.sh')
assert script.parent.is_dir(),'run inside the EverIOS workspace'
script.write_text(s.replace('@RUN_NONCE@',nonce))
subprocess.run(['sh','-n',str(script)],check=True)
print(script)
print('Expected completion token:',noncefile)
