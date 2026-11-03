#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$here/tests/out"
mkdir -p "$out"
export TMPDIR=/tmp
for name in IpaMmio.h IpaFirmware.h IpaProbe.inc; do
    python3 "$here/../modem/extract.py" "$here/../../patches/ipa" "$out/$name" "bsd/dev/modem/$name"
done
for name in QcomSmem.h QcomSmem.c; do
    python3 "$here/../modem/extract.py" "$here/../../patches/modem" "$out/$name" "bsd/dev/modem/$name"
done
cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -DMODEM_PROTOCOL_HOST_TEST -I"$out" "$here/tests/probe.c" "$out/QcomSmem.c" -o "$out/probe"
ASAN_OPTIONS=detect_leaks=0 "$out/probe"
