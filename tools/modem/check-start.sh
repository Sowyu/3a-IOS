#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
for file in ModemRuntime.inc ModemGate.h ModemRmtfs.c ModemRmtfs.h QcomQrtr.c QcomQrtr.h QcomSmp2p.c QcomSmp2p.h QcomScm.h QcomQmp.c QcomQmp.h QcomRpmh.c QcomRpmh.h; do
 python3 "$root/extract.py" "$root/../../patches/modem" "$out/$file" "bsd/dev/modem/$file"
done
for pair in 'qrtr' 'rmtfs'; do
 set -- $pair
 cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$root/tests/$1.c" "$out/ModemRmtfs.c" "$out/QcomQrtr.c" -o "$out/check-$1"
 "$out/check-$1"
done
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$root/tests/control.c" "$out/QcomQmp.c" "$out/QcomSmp2p.c" "$out/QcomRpmh.c" -o "$out/check-control"
"$out/check-control"
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:])
s=(out/'ModemRuntime.inc').read_text()
def function(start,end): return s[s.index(start):s.index(end)]
(out/'lifecycle.c').write_text((root/'tests/lifecycle-prefix.c').read_text()+function('static int m_scm(','static int m_assign(')+function('static bool m_cleanup(','static int m_power_inspect(')+(root/'tests/lifecycle-tail.c').read_text())
(out/'power.c').write_text((root/'tests/power-prefix.c').read_text()+function('static int m_power_inspect(', '#include "ModemGate.h"')+(root/'tests/power-tail.c').read_text())
(out/'shutdown.c').write_text((root/'tests/shutdown-prefix.c').read_text()+function('static void m_shutdown_receive(', '/* Insert before m_greceive')+function('static bool m_shutdown_request(', 'static int m_shadow_prepare(')+(root/'tests/shutdown-tail.c').read_text())
(out/'vote.c').write_text((root/'tests/vote-prefix.c').read_text()+function('static int m_vote(', 'static int m_qwait(')+(root/'tests/vote-tail.c').read_text())
PY
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$out/lifecycle.c" -o "$out/check-lifecycle"
"$out/check-lifecycle"
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$out/vote.c" -o "$out/check-vote"
"$out/check-vote"
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$out/power.c" "$out/QcomRpmh.c" -o "$out/check-power"
"$out/check-power"
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$out/shutdown.c" "$out/QcomQrtr.c" -o "$out/check-shutdown"
"$out/check-shutdown"
python3 - "$out" <<'PYCHK'
from pathlib import Path
import re,sys
out=Path(sys.argv[1])
for name in ['ModemRuntime.inc','ModemRmtfs.c']:
 s=(out/name).read_text()
 assert set(re.findall(r'\b(sweet_ufs_\w+)\s*\(',s)) <= {'sweet_ufs_read_part','sweet_ufs_part_size'}
runtime=(out/'ModemRuntime.inc').read_text()
assert runtime.index('!m_gate("sweetmpss")') < runtime.index('uint32_t outoff;if(m_alloc')
print('PASS: runtime storage references are read-only; rmtfs has no storage API')
PYCHK

python3 "$root/check-shutdown-joined.py"
