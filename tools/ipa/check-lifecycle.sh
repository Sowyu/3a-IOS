#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$here/tests/out"
mkdir -p "$out"
export TMPDIR=/tmp
for name in IpaMmio.h IpaFirmware.h IpaSmmu.h IpaMmu.h IpaPower.h IpaRuntime.inc; do
    python3 "$here/../modem/extract.py" "$here/../../patches/ipa" "$out/$name" "bsd/dev/modem/$name"
done
for name in QcomScm.h QcomRpmh.h; do
    python3 "$here/../modem/extract.py" "$here/../../patches/modem" "$out/$name" "bsd/dev/modem/$name"
done
for name in smmu mmu tlb runtime; do
    cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$here/tests/$name.c" -o "$out/$name"
    ASAN_OPTIONS=detect_leaks=0 "$out/$name"
done
