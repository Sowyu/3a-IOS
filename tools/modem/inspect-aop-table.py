#!/usr/bin/env python3
"""Read-only candidate ARC table analysis. Does not prove the live command DB."""
from pathlib import Path
import re,struct
p=Path(__file__).resolve().parents[3]/'backup-asteroids/aop_a.img'
b=p.read_bytes();candidates=[]
for hit in re.finditer(b'cx.lvl\x00\x00',b):
 at=hit.start();addr,length,offset=struct.unpack_from('<IHH',b,at+16)
 if addr==0x30000 and length==32 and offset==0:candidates.append(at)
assert len(candidates)==1
start=candidates[0];entries=[]
for i in range(64):
 at=start+i*24;name=b[at:at+8].split(b'\0')[0]
 if not re.fullmatch(rb'[a-z0-9]+\.(lvl|tmr)',name):break
 addr,length,offset=struct.unpack_from('<IHH',b,at+16)
 assert addr>>16==3 and length in (0,32)
 entries.append((name.decode(),addr,length,offset))
span=max(off+length for _,_,length,off in entries);base=start-span
print(f'Inferred AOP ARC aux block {base:#x}, headers {start:#x}, entries {len(entries)}')
for name,addr,length,off in entries:
 if name in ('cx.lvl','mss.lvl','xo.lvl'):
  levels=struct.unpack_from('<'+str(length//2)+'H',b,base+off)
  print(f'{name}: address={addr:#x} aux_bytes={length} values='+','.join(map(str,levels)))
print('Live power-probe comparison required before using this as hardware evidence.')
