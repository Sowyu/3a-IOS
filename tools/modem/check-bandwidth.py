#!/usr/bin/env python3
"""Check the exact production bandwidth encoder and RSC transaction on host."""
from pathlib import Path
import os,subprocess
here=Path(__file__).resolve().parent;out=here/'tests/out';s=(out/'ModemRuntime.inc').read_text()
code=(here/'tests/bandwidth-prefix.c').read_text()+s[s.index('static int m_bandwidth_vote('):s.index('static int m_vote(')]+(here/'tests/bandwidth-tail.c').read_text()
(out/'bandwidth.c').write_text(code)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'bandwidth.c'),str(out/'QcomRpmh.c'),'-o',str(out/'check-bandwidth')],env=env,check=True)
subprocess.run([str(out/'check-bandwidth')],env=env,check=True)
assert s.index('if((rc=m_bandwidth_vote()))goto out;')<s.index('if((rc=m_load(f,mdt,size)))goto out;')
assert 'm.bandwidth=m_gate("sweetmbw")' in s
