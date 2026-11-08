#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/battery"
export TMPDIR=/tmp
for name in BatteryRead.c BatteryRead.h BatterySnapshot.h BatterySnapshot.inc; do
 python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/$name" "bsd/dev/battery/$name"
done
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/battery" "$root/tests/read.c" -o "$out/read"
"$out/read"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
out=Path(sys.argv[1]);s=(out/'battery/BatterySnapshot.inc').read_text();assert s.count('#include <kern/locks.h>')==1
(out/'snapshot-source.inc').write_text(s.replace('#include <kern/locks.h>','/* host lock mock */'))
PY
cc -save-temps=obj -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" -I"$out/battery" "$root/tests/snapshot.c" "$out/battery/BatteryRead.c" -o "$out/snapshot"
"$out/snapshot"
# Negative control: before 2199 the publisher negated current; the ast192 cycles must fail.
mkdir -p "$out/patches-presign" "$out/presign"
cp "$root/../../patches/battery"/219[0-8]-*.patch "$out/patches-presign/"
python3 "$root/extract.py" "$out/patches-presign" "$out/presign/BatterySnapshot.inc" bsd/dev/battery/BatterySnapshot.inc
sed 's|#include <kern/locks.h>|/* host lock mock */|' "$out/presign/BatterySnapshot.inc" > "$out/presign/snapshot-source.inc"
cp "$out/battery/BatterySnapshot.h" "$out/battery/BatteryRead.h" "$out/presign/"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/presign" "$root/tests/snapshot.c" "$out/battery/BatteryRead.c" -o "$out/snapshot-presign"
if neg=$("$out/snapshot-presign" 2>&1); then echo "FAIL: pre-2199 publisher passed the ast192 sign test"; exit 1; fi
printf '%s\n' "$neg" | grep -q 's.milliamps==ast192\[i\].ma' || { printf '%s\n' "$neg"; echo "FAIL: pre-2199 publisher failed for another reason"; exit 1; }
echo "PASS: negative control, pre-2199 publisher fails the ast192 current sign"
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/AdspRuntime.inc" bsd/dev/battery/AdspRuntime.inc
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/QcomGlink.h" bsd/dev/battery/QcomGlink.h
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:]);s=(out/'battery/AdspRuntime.inc').read_text()
# 2291: the daily constants, stop flag and adsp_read_fail sit before m_bstate.
a=s.index('#define B_CYCLE_TEST_MS');b=s.index('static void m_bstate',a);daily=s[a:b]
a=s.index('static void m_breceive');b=s.index('#include "AdspHelpers.inc"',a);receive=s[a:b]
a=s.index('static void adsp_read_poll');b=s.index('static int adsp_open_images',a);poll=s[a:b]
(out/'read-runtime.c').write_text((root/'tests/read-runtime-prefix.c').read_text()+daily+receive+poll+(root/'tests/read-runtime-tail.c').read_text())
PY
cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/battery" "$out/read-runtime.c" -o "$out/read-runtime"
"$out/read-runtime"
# Negative controls: the series before 2295 ignores notifications, before 2296 never probes, before 2297 never kicks.
neg_runtime() {   # $1 tag, $2 patches before the feature, $3 control tail, $4 message the failure must name
 d="$out/neg-$1"; mkdir -p "$d/p" "$d/battery"
 for p in $2; do cp "$p" "$d/p/"; done
 for name in AdspRuntime.inc BatteryRead.c BatteryRead.h QcomGlink.h; do python3 "$root/extract.py" "$d/p" "$d/battery/$name" "bsd/dev/battery/$name"; done
 python3 - "$root" "$d" "$3" <<'PY'
from pathlib import Path
import sys
root,d,tail=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3];s=(d/'battery/AdspRuntime.inc').read_text()
a=s.index('#define B_CYCLE_TEST_MS');b=s.index('static void m_bstate',a);daily=s[a:b]
a=s.index('static void m_breceive');b=s.index('#include "AdspHelpers.inc"',a);receive=s[a:b]
a=s.index('static void adsp_read_poll');b=s.index('static int adsp_open_images',a);poll=s[a:b]
(d/'control.c').write_text((root/'tests/read-runtime-prefix.c').read_text()+daily+receive+poll+(root/'tests'/tail).read_text())
PY
 cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -fsanitize=address,undefined -I"$d/battery" "$d/control.c" -o "$d/control"
 if neg=$("$d/control" 2>&1); then echo "FAIL: $1 control passed"; exit 1; fi
 printf '%s\n' "$neg" | grep -q "$4" || { printf '%s\n' "$neg"; echo "FAIL: $1 control failed for another reason"; exit 1; }
 echo "PASS: negative control, $1"
}
P="$root/../../patches/battery"
neg_runtime pre-2295 "$P/219[0-9]-*.patch $P/229[0-4]-*.patch" read-runtime-notify-control.c "notification starts a read"
neg_runtime pre-2296 "$P/219[0-9]-*.patch $P/229[0-5]-*.patch" read-runtime-probe-control.c "probe sent between cycles"
d="$out/neg-pre-2297"; mkdir -p "$d/p" "$d/battery"
for p in "$P"/219[0-9]-*.patch "$P"/229[0-6]-*.patch; do cp "$p" "$d/p/"; done
for name in BatterySnapshot.inc BatterySnapshot.h BatteryRead.h BatteryRead.c; do python3 "$root/extract.py" "$d/p" "$d/battery/$name" "bsd/dev/battery/$name"; done
sed 's|#include <kern/locks.h>|/* host lock mock */|' "$d/battery/BatterySnapshot.inc" > "$d/battery/snapshot-source.inc"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$d/battery" "$root/tests/snapshot.c" "$d/battery/BatteryRead.c" -o "$d/snapshot"
if neg=$("$d/snapshot" 2>&1); then echo "FAIL: pre-2297 control passed"; exit 1; fi
printf '%s\n' "$neg" | grep -q "kick on the first sample" || { printf '%s\n' "$neg"; echo "FAIL: pre-2297 control failed for another reason"; exit 1; }
echo "PASS: negative control, pre-2297"
# 2291: the real sweet_adsp.c gate, worker and shutdown handshake.
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/sweet_adsp.c" bsd/dev/sweet_adsp.c
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/ModemGate.h" bsd/dev/battery/ModemGate.h
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:]);s=(out/'battery/sweet_adsp.c').read_text()
body=s[s.index('extern kern_return_t thread_terminate'):s.rindex('#endif')]
body=body.replace('extern kern_return_t thread_terminate(thread_t);\n','')
(out/'adsp-gate.c').write_text((root/'tests/adsp-gate-prefix.c').read_text()+body+(root/'tests/adsp-gate-tail.c').read_text())
PY
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/battery" "$out/adsp-gate.c" -o "$out/adsp-gate"
"$out/adsp-gate"
