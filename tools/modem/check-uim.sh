#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
for file in ModemRuntime.inc ModemUim.c ModemUim.h QcomQrtr.c QcomQrtr.h; do
 python3 "$root/extract.py" "$root/../../patches/modem" "$out/$file" "bsd/dev/modem/$file"
done
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$root/tests/uim.c" "$out/ModemUim.c" "$out/QcomQrtr.c" -o "$out/check-uim"
"$out/check-uim"
python3 - "$root" "$out" <<'PYTEST'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:]);s=(out/'ModemRuntime.inc').read_text()
code=s[s.index('static bool m_uim_endpoint_valid('):s.index('static void m_greceive(')]
assert 'if(m.uim && (m.state&6)==6)m_uim_poll()' in s
(out/'uim-runtime.c').write_text((root/'tests/uim-runtime-prefix.c').read_text()+code+(root/'tests/uim-runtime-tail.c').read_text())
PYTEST
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$out/uim-runtime.c" "$out/ModemUim.c" "$out/QcomQrtr.c" -o "$out/check-uim-runtime"
"$out/check-uim-runtime"
