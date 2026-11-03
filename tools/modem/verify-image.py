#!/usr/bin/env python3
"""Verify every packed kernel segment and report hashes without operating a phone."""
import hashlib,importlib.util,struct,sys,zlib
from pathlib import Path
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('mkpayload',root/'ios-asteroids/tools/mkpayload.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
kpath=root/'ios-asteroids/research/xnu-modem/dxb/fakeroot/System/Library/Kernels/kernel.development.sweet'
original=kpath.read_bytes();kernel=bytearray(original);m=module.parse_macho(kernel);module.apply_chained_fixups(kernel,m)
for name in sys.argv[1:]:
 payload=(root/'plans/modem/out'/f'{name}.payload').read_bytes();image=(root/'plans/modem/out'/f'{name}-boot.img').read_bytes()
 raw=zlib.decompress(image[4096:],31);assert payload in raw
 assert payload[:8]==b'SWEETXNU' and struct.unpack_from('<I',payload,8)[0]==1
 count=struct.unpack_from('<I',payload,12)[0]
 for i in range(count):
  va,size,offset,length=struct.unpack_from('<4Q',payload,0x2a8+32*i)
  segment=next(s for s in m['segs'] if s['vmaddr']==va and s['vmsize']==size)
  assert length==segment['filesize'] and size==segment['vmsize']
  assert payload[offset:offset+length]==kernel[segment['fileoff']:segment['fileoff']+length]
 print(name,'PASS',count,'rebased kernel segments; kernel',hashlib.sha256(original).hexdigest(),'image',hashlib.sha256(image).hexdigest())
