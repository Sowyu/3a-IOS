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
# 2298: the sampler's frame walk, extracted from the patched osfmk/kern/processor.c hunk.
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import re,sys
root,out=map(Path,sys.argv[1:])
patch=(root/'../../patches/battery/2298-asteroids-cputop-hog-pc.patch').read_text()
added=''.join(l[1:]+'\n' for l in patch.splitlines() if l.startswith('+') and not l.startswith('+++'))
a=added.index('static unsigned\nsct_walk(');b=added.index('static void\nsct_pc_xcall',a)
(out/'cputop-walk.inc').write_text(added[a:b])
PY
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$root/tests/cputop-walk.c" -o "$out/cputop-walk"
"$out/cputop-walk"
# 2300: register_serial_functions, new side passes, old side (the ast197 self-loop) must fail.
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:])
lines=(root/'../../patches/battery/2300-asteroids-serial-keyboard-spin.patch').read_text().splitlines()
def side(keep):
    body=[l[1:] for l in lines if l[:1] in (' ',keep) and not l.startswith(('+++','---'))]
    t='\n'.join(body);a=t.index('register_serial_functions(struct pe_serial_functions *fns)');b=t.index('\n}\n',a)+3
    return 'static void\n'+t[a:b]
(out/'new').mkdir(exist_ok=True);(out/'old').mkdir(exist_ok=True)
(out/'new/serial-register.inc').write_text(side('+'));(out/'old/serial-register.inc').write_text(side('-'))
PY
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/new" "$root/tests/serial-register.c" -o "$out/serial-register"
"$out/serial-register"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/old" "$root/tests/serial-register.c" -o "$out/serial-register-old"
if neg=$("$out/serial-register-old" 2>&1); then echo "FAIL: pre-2300 registration passed"; exit 1; fi
printf '%s\n' "$neg" | grep -q "second registration of the same device makes no cycle" || { printf '%s\n' "$neg"; echo "FAIL: pre-2300 control failed for another reason"; exit 1; }
echo "PASS: negative control, pre-2300 registration loops the list, as in ast197 and ast203"
