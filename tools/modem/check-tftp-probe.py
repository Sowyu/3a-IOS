#!/usr/bin/env python3
"""Test reconstructed production parser, gate and receive callback without hardware."""
from pathlib import Path
import os, re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[3]
public=root/'3a-IOS'
source=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else None
out=Path(tempfile.mkdtemp(prefix='modem-tftp-',dir='/tmp'))
os.environ['TMPDIR']='/tmp'
for name in ['ModemRuntime.inc','ModemTftpProbe.h','ModemGate.h']:
    if source:(out/name).write_bytes((source/name).read_bytes())
    else:subprocess.run([sys.executable,'-B',str(public/'tools/modem/extract.py'),str(public/'patches/modem'),str(out/name),'bsd/dev/modem/'+name],check=True)
s=(out/'ModemRuntime.inc').read_text()
start=s.index('static void m_tftp_probe_receive(');end=s.index('static void m_pd_receive(',start)
callback=s[start:end]
assert 'qcom_qrtr_add_service(&m.qr,4096,1,0x4006,m_tftp_probe_receive,0)' in s
assert s.index('!modem_tftp_probe_allowed(PE_boot_args())')<s.index('    m_sleep(30000);')<s.index('m.smem=ml_io_map(')
assert 'if(m.tftp_probe && qcom_qrtr_add_service' in s
assert set(re.findall(r'\b(sweet_ufs_\w+)\s*\(',s))<={'sweet_ufs_read_part','sweet_ufs_part_size'}
header=(out/'ModemTftpProbe.h').read_text()
assert not re.search(r'\b(?:sweet_ufs_\w+|vnode_\w+|fopen|write|open)\s*\(',callback+header)
tests=public/'tools/modem/tests'
(out/'parser.c').write_bytes((tests/'tftp-probe.c').read_bytes())
(out/'worker.c').write_text((tests/'tftp-probe-prefix.c').read_text()+callback+(tests/'tftp-probe-tail.c').read_text())
for name in ['parser','worker']:
    subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(out),str(out/(name+'.c')),'-o',str(out/name)],cwd=out,check=True)
    r=subprocess.run([str(out/name)],capture_output=True,text=True,check=True)
    print(r.stdout.splitlines()[-1])
print('PASS actual service registration, gate before hardware, no TFTP storage interface;',out)
