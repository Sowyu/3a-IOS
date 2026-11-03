#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
for file in QcomSmem.c QcomSmem.h QcomGlink.c QcomGlink.h; do
  python3 "$root/extract.py" "$root/../../patches/modem" "$out/$file" "bsd/dev/modem/$file"
done
for pair in 'smem QcomSmem' 'glink QcomGlink'; do
  set -- $pair
  cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined \
    -I"$out" "$root/tests/$1.c" "$out/$2.c" -o "$out/check-$1"
  "$out/check-$1"
done
