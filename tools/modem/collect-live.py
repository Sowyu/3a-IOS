#!/usr/bin/env python3
"""Read filtered kernel messages only while our selected devloop is alive."""
from pathlib import Path
import os,re,subprocess,sys,time
os.environ['TMPDIR']='/tmp'
root=Path(__file__).resolve().parents[3]
iface=sys.argv[2] if len(sys.argv)>2 else 'en8';assert re.fullmatch(r'(en|bridge)[0-9]+',iface)
name=sys.argv[1];base=name.rsplit('-',1)[0] if name.rsplit('-',1)[-1].isdigit() else name;assert base in ('power-probe','offline-usb','uim-usb','bcm-probe','data-usb')
ps=subprocess.run(['ps','-axo','pid=,command='],capture_output=True,text=True,check=True).stdout
pids=[int(line.split(None,1)[0]) for line in ps.splitlines() if '/plans/devloop/'+'dev.py' in line and name+'-boot.img' in line]
assert len(pids)==1,'exactly one selected phone loop required'
pid=pids[0];out=root/'plans/modem/logs'/('live-'+name+'-'+time.strftime('%Y%m%d-%H%M%S')+'.txt');seen=set();end=time.monotonic()+(840 if base=='data-usb' else 540)
ssh=['ssh','-T','-i',str(Path.home()/'.ssh/id_ed25519'),'-o','BatchMode=yes','-o','StrictHostKeyChecking=no','-o','UserKnownHostsFile=/dev/null','-o','ConnectTimeout=3','-o','ConnectionAttempts=1','root@fe80::5eff:fe77:ee02%'+iface,"/var/jb/sbin/dmesg 2>/dev/null | grep -E 'SWEET MODEM|SWEET IPA|SWEET DATA|SWEET TOUCH: (SN3|RPMh SN3)'; echo MODEMSSH-READY"]
with out.open('w') as f:
 while time.monotonic()<end:
  try:os.kill(pid,0)
  except ProcessLookupError:break
  try:r=subprocess.run(ssh,capture_output=True,text=True,timeout=12)
  except subprocess.TimeoutExpired:r=None
  if r and r.returncode==0:
   for line in r.stdout.splitlines():
    if line not in seen:seen.add(line);f.write(line+'\n');f.flush()
  time.sleep(5)
print(out,'collected',len(seen),'distinct lines while selected loop was alive')
