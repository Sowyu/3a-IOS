#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$here/tests/out"
mkdir -p "$out"
export TMPDIR=/tmp
python3 "$here/../modem/extract.py" "$here/../../patches/ipa" "$out/IpaMmio.h" "bsd/dev/modem/IpaMmio.h"
xcrun clang -target arm64-none-elf -ffreestanding -O2 -S -I"$out" "$here/tests/mmio.c" -o "$out/mmio.s"
python3 - "$out/mmio.s" <<'PY'
import re,sys
s=open(sys.argv[1]).read()
ops=re.findall(r'^\s*(ldr|str)\s+([^\n]+)',s,re.M)
assert len(ops)==3,ops
assert ops[0][0]=='ldr' and re.fullmatch(r'w\d+, \[x\d+\]',ops[0][1]),ops
assert ops[1][0]=='str' and re.fullmatch(r'w\d+, \[x\d+\]',ops[1][1]),ops
assert ops[2][0]=='str' and re.fullmatch(r'x\d+, \[x\d+\]',ops[2][1]),ops
assert len(re.findall(r'\bdmb\s+sy',s))==3 and len(re.findall(r'\bdsb\s+sy',s))==2
print('PASS: ARM64 production MMIO accessors use only plain ldr/str with barriers')
PY
