#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out"
for file in ModemRuntime.inc ModemGate.h ModemRmtfs.c ModemRmtfs.h QcomQrtr.c QcomQrtr.h QcomSmp2p.c QcomSmp2p.h QcomQmp.c QcomQmp.h QcomRpmh.c QcomRpmh.h; do
 python3 "$root/extract.py" "$root/../../patches/modem" "$out/$file" "bsd/dev/modem/$file"
done
for pair in 'qrtr' 'rmtfs'; do
 set -- $pair
 cc -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$root/tests/$1.c" "$out/ModemRmtfs.c" "$out/QcomQrtr.c" -o "$out/check-$1"
 "$out/check-$1"
done
cc -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$root/tests/control.c" "$out/QcomQmp.c" "$out/QcomSmp2p.c" "$out/QcomRpmh.c" -o "$out/check-control"
"$out/check-control"
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:])
s=(out/'ModemRuntime.inc').read_text()
def function(start,end): return s[s.index(start):s.index(end)]
(out/'lifecycle.c').write_text((root/'tests/lifecycle-prefix.c').read_text()+function('static int m_scm(','static int m_pas(')+function('static bool m_cleanup(','#include "ModemGate.h"')+(root/'tests/lifecycle-tail.c').read_text())
(out/'vote.c').write_text((root/'tests/vote-prefix.c').read_text()+function('static int m_vote(', 'static int m_qwait(')+(root/'tests/vote-tail.c').read_text())
PY
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$out/lifecycle.c" -o "$out/check-lifecycle"
"$out/check-lifecycle"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$out/vote.c" -o "$out/check-vote"
"$out/check-vote"
python3 - "$out" <<'PYCHK'
from pathlib import Path
import re,sys
out=Path(sys.argv[1])
for name in ['ModemRuntime.inc','ModemRmtfs.c']:
 s=(out/name).read_text()
 assert set(re.findall(r'\b(sweet_ufs_\w+)\s*\(',s)) <= {'sweet_ufs_read_part','sweet_ufs_part_size'}
print('PASS: runtime storage references are read-only; rmtfs has no storage API')
PYCHK
