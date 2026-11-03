#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/battery"
export TMPDIR=/tmp
for name in AdspRuntime.inc AdspHelpers.inc AdspFirmware.inc QcomGlink.c QcomGlink.h QcomQrtr.c QcomQrtr.h QcomQmp.c QcomQmp.h QcomScm.h; do
 python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/$name" "bsd/dev/battery/$name"
done
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:]);s=(out/'battery/AdspRuntime.inc').read_text()
a=s.index('static bool adsp_cleanup');b=s.index('static int adsp_run',a)
(out/'adsp-cleanup.c').write_text((root/'tests/adsp-cleanup-prefix.c').read_text()+s[a:b]+(root/'tests/adsp-cleanup-tail.c').read_text())
a=s.index('static int adsp_load_image');b=s.index('static bool adsp_cleanup',a)
load=s[a:b].replace('__asm__ volatile("dsb sy":::"memory");','/* Host barrier omitted. */')
(out/'adsp-load.c').write_text((root/'tests/adsp-load-prefix.c').read_text()+load+(root/'tests/adsp-load-tail.c').read_text())
h=(out/'battery/AdspHelpers.inc').read_text();a=h.index('static int m_vote');b=h.index('static int m_qwait',a)
vote=h[a:b];a=h.index('static void m_smp_poll');b=h.index('static int m_qsend_packet',a)
(out/'adsp-vote.c').write_text((root/'tests/adsp-vote-prefix.c').read_text()+vote+h[a:b]+(root/'tests/adsp-vote-tail.c').read_text())
PY
cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined "$out/adsp-cleanup.c" -o "$out/adsp-cleanup"
"$out/adsp-cleanup"
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$out/adsp-load.c" -o "$out/adsp-load"
"$out/adsp-load"
for pair in 'glink QcomGlink' 'qrtr QcomQrtr'; do
 set -- $pair
 cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out/battery" "$root/tests/$1.c" "$out/battery/$2.c" -o "$out/$1"
 "$out/$1"
done

cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$out/adsp-vote.c" -o "$out/adsp-vote"
"$out/adsp-vote"
