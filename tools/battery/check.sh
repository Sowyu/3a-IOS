#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/battery"
export TMPDIR=/tmp
python3 "$root/extract.py" "$root/../../patches/battery" "$out/sweet_battery.c" bsd/dev/sweet_battery.c
for name in QcomSmem.c QcomSmem.h QcomSmp2p.c QcomSmp2p.h QcomRpmh.c QcomRpmh.h ModemGate.h; do
 python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/$name" "bsd/dev/battery/$name"
done
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DBATTERY_HOST_TEST -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$root/tests" -I"$out" "$root/tests/probe.c" "$out/battery/QcomSmem.c" "$out/battery/QcomSmp2p.c" "$out/battery/QcomRpmh.c" -o "$out/probe"
"$out/probe"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
out=Path(sys.argv[1]);s=(out/'battery/QcomSmem.c').read_text()
needle='    return *(volatile uint32_t *)(smem->base + offset);'
assert s.count(needle)==1
(out/'smem-race-source.c').write_text(s.replace(needle,'    before_read(offset);\n'+needle))
PY
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" -I"$out/battery" "$root/tests/smem-race.c" -o "$out/smem-race"
"$out/smem-race"
