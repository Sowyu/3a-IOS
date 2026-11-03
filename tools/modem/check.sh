#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out"
python3 - "$root/../../patches/modem/2170-asteroids-modem-preflight.patch" "$out/sweet_modem.c" <<'PYCODE'
import sys
from pathlib import Path
text = Path(sys.argv[1]).read_text().splitlines(True)
active = False
lines = []
for line in text:
    if line.startswith('+++ '):
        active = line.strip() == '+++ b/bsd/dev/sweet_modem.c'
    elif active and line.startswith('+'):
        lines.append(line[1:])
assert lines
Path(sys.argv[2]).write_text(''.join(lines))
PYCODE
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$root/tests" -I"$out" \
  "$root/tests/check.c" -o "$out/check"
ASAN_OPTIONS=detect_leaks=0 "$out/check"
