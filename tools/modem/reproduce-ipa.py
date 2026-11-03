"""Reconstruct combined modem/data/IPA files from pristine XNU and ordered patches."""
from pathlib import Path
import subprocess as sp, re, time, hashlib, sys
R=Path(__file__).resolve().parents[3]
X=R/'ios-asteroids/research/xnu-modem/dxb/xnu'
D=R/'plans/modem/series-check'/str(time.time_ns());D.mkdir(parents=True)
cutoff=int(sys.argv[1]) if len(sys.argv)>1 else 2208
data=(R/sys.argv[2]).resolve() if len(sys.argv)>2 else R/'plans/modem/snapshots/data'
assert data.is_relative_to(R) and data.is_dir(),data
ours=sorted(p for p in (R/'3a-IOS/patches/ipa').glob('22*.patch') if int(p.name[:4])<=cutoff)
owners=sorted((R/'plans/modem/patches').glob('21*.patch'))+sorted(data.glob('222*.patch'))
paths=sorted({s for p in ours+owners for s in re.findall(r'^\+\+\+ b/(.+)$',p.read_text(),re.M)})
for name in paths:
 result=sp.run(['git','show','HEAD:'+name],cwd=X,capture_output=True)
 if result.returncode==0:
  dest=D/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(result.stdout)
series=sorted((R/'ios-asteroids/research/xnu-modem/dxb/patches/26.0').glob('*.patch'))
series+=[R/'plans/freeform/2130-asteroids-app-metal-exec-v4.patch']
series+=sorted((R/'plans/lag/patches').glob('214*.patch'))
for prefix in ['2150-','2151-','2153-','2152-asteroids-fast-power-v4','2154-']:
 matches=list((R/'plans/input/patches').glob(prefix+'*.patch'));assert len(matches)==1;series+=matches
series+=sorted((R/'3a-IOS/patches/usb').glob('*-v5.patch'))
series+=sorted((R/'plans/modem/patches').glob('21*.patch'))
series+=sorted(data.glob('222*.patch'))
series+=ours
for p in series:
 data=p.read_bytes().decode("latin1")
 if not any('+++ b/'+name+'\n' in data for name in paths):continue
 assert '+++ /dev/null' not in data and 'deleted file mode' not in data,p
 data=re.sub(r'(diff --git [^\n]+\n)(--- /dev/null\n)',r'\1new file mode 100644\n\2',data)
 result=sp.run(['git','apply',*[f'--include={name}' for name in paths],'-'],input=data.encode("latin1"),cwd=D,capture_output=True)
 if result.returncode:raise SystemExit(str(p)+'\n'+result.stderr.decode())
 print(hashlib.sha256(p.read_bytes()).hexdigest(),p.relative_to(R))
for name in paths:
 assert (D/name).read_bytes()==(X/name).read_bytes(),name
print('PASS ordered patches reproduce',len(paths),'live combined files exactly;',D)
