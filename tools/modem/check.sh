#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
python3 "$root/extract.py" "$root/../../patches/modem" "$out/sweet_modem.c"
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/tests" -I"$out" "$root/tests/check.c" -o "$out/check"
ASAN_OPTIONS=detect_leaks=0 "$out/check"
