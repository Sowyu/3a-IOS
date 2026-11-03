#!/usr/bin/env python3
"""Check that incomplete shutdown cannot pass modem-run acceptance."""
from pathlib import Path
import json,subprocess,sys,tempfile
here=Path(__file__).resolve().parent
out=here/'out';out.mkdir(exist_ok=True)
tmp=Path(tempfile.mkdtemp(prefix='acceptance-',dir=out))
try:
 run=tmp/'run';(run/'frames').mkdir(parents=True)
 (run/'result.json').write_text(json.dumps(dict(result='selftest-done',root_state='clean',stale=False,run_seconds=360)))
 nonce='a'*32;(tmp/'nonce').write_text(nonce)
 (run/'modem-offline-complete.txt').write_text('nonce='+nonce+' phase=body-complete shot=0')
 (run/'frames/01-modem_offline.png').write_bytes(b'host-test-placeholder')
 log='''PAS4 cmd=5 status=0 result=0
SMP2P slave=0x6 ready=1 handover=1
GLINK mpss/IPCRTR up=1
QRTR NEW_SERVER service=0xb instance=1
pd-mapper response=55
rmtfs msg=3 reads=1 RAM_writes=0 response=14 error=0
PAS4 cmd=6 status=0 result=0
STOP verified PAS; cleanup=ok ready=1 handover=1 services=45
gated modem worker finished final_error=0
SELFTEST: DONE
'''
 def check(text):
  run.with_suffix('.txt').write_text(text)
  return subprocess.run([sys.executable,str(here.parent/'check-run.py'),str(run),'offline',str(tmp/'nonce')],capture_output=True).returncode
 assert check(log)==0
 assert check(log+'STOP UNVERIFIED\n')!=0
 assert check(log.replace('final_error=0','final_error=5'))!=0
 assert check(log.replace('cleanup=ok','cleanup=INCOMPLETE'))!=0
 assert check(log.replace('PAS4 cmd=6 status=0','PAS4 cmd=6 status=-3145800'))!=0
 (tmp/'nonce').write_text('b'*32);assert check(log)!=0
 print('PASS run acceptance rejects unverified/incomplete stop, failed PAS/final status and stale marker')
finally:
 subprocess.run(['/usr/bin/trash',str(tmp)],check=True)
