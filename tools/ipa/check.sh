#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$here/tests/out"
mkdir -p "$out"
export TMPDIR=/tmp
python3 "$here/../modem/extract.py" "$here/../../patches/ipa" "$out/IpaFirmware.h" bsd/dev/modem/IpaFirmware.h
python3 "$here/../modem/extract.py" "$here/../../patches/modem" "$out/ModemGate.h" bsd/dev/modem/ModemGate.h
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$here/tests/firmware.c" -o "$out/firmware"
ASAN_OPTIONS=detect_leaks=0 "$out/firmware" "$@"
