# VA -> bytes over a split dyld cache (from plans/blackbars2/tools/dsc-export.py). Read-only.
import mmap, os, struct
u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
u64 = lambda b, o: struct.unpack_from('<Q', b, o)[0]
def _load(p):
    f = open(p, 'rb'); return mmap.mmap(f.fileno(), 0, prot=mmap.PROT_READ)
class Cache:
    def __init__(self, base):
        main = _load(base); files = [main]
        n, off = u32(main, 0x18c), u32(main, 0x188)
        for i in range(n):
            e = off + i * 56
            suffix = bytes(main[e + 24:e + 56]).split(b'\0')[0].decode()
            if os.path.exists(base + suffix): files.append(_load(base + suffix))
        self.maps = []
        for f in files:
            for i in range(u32(f, 0x14)):
                m = u32(f, 0x10) + i * 32
                self.maps.append((u64(f, m), u64(f, m + 8), u64(f, m + 16), f))
    def rd(self, va, n):
        for a, s, o, f in self.maps:
            if a <= va < a + s: return bytes(f[o + va - a:o + va - a + n])
        raise KeyError(hex(va))
