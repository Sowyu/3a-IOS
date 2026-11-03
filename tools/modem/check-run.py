#!/usr/bin/env python3
"""Require measured modem stages, not merely a successful devloop exit."""
from pathlib import Path
import json,re,sys
p=Path(sys.argv[1]);mode=sys.argv[2];assert mode in ('offline','uim')
r=json.loads((p/'result.json').read_text());text=p.with_suffix('.txt').read_text(errors='replace')
assert r['result']=='selftest-done' and r['root_state']=='clean' and not r['stale']
assert not re.search(r'SELFTEST: (FAIL|SKIP)|panic\(cpu|deadman:.*(?:reboot|reset)|STOP UNVERIFIED|cleanup=INCOMPLETE',text)
checks=[r'PAS4 cmd=5 status=0 result=0',r'SMP2P slave=.*ready=1 handover=1',r'GLINK mpss/IPCRTR up=1',r'QRTR NEW_SERVER service=0xb ',r'pd-mapper response=[1-9][0-9]*',r'rmtfs msg=3 reads=[1-9][0-9]* .*error=0',r'PAS4 cmd=6 status=0 result=0',r'STOP verified PAS; cleanup=ok ready=1 handover=1 services=[1-9][0-9]*',r'gated modem worker finished final_error=0',r'SELFTEST: DONE']
if mode=='uim':
 checks += [r'UIM read request msg=0x2f ',r'UIM card-status error=0 slots=[1-4]',r'UIM slot=[1-4] card=(?:PRESENT|ABSENT)']
 if re.search(r'UIM slot=[1-4] card=PRESENT',text):
  checks += [r'UIM slot=[1-4] app=[0-7] type=[0-9]+ state=[0-9]+',r'UIM physical=[1-4] .*ICCID_suffix4=[0-9]{4}\b']
for pattern in checks:
 match=re.search(pattern,text);assert match,'missing '+pattern
 print(str(p.with_suffix('.txt'))+':'+str(text[:match.start()].count('\n')+1),match.group())
print('PASS',mode,'stages, clean shutdown and root; elapsed',r['run_seconds'],'seconds')
