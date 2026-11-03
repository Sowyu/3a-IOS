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
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/AdspRuntime.inc" bsd/dev/battery/AdspRuntime.inc
python3 "$root/extract.py" "$root/../../patches/battery" "$out/battery/QcomGlink.h" bsd/dev/battery/QcomGlink.h
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root,out=map(Path,sys.argv[1:]);s=(out/'battery/AdspRuntime.inc').read_text()
a=s.index('static void m_breceive');b=s.index('#include "AdspHelpers.inc"',a);receive=s[a:b]
a=s.index('static void adsp_read_poll');b=s.index('static int adsp_open_images',a)
(out/'read-runtime.c').write_text((root/'tests/read-runtime-prefix.c').read_text()+receive+s[a:b]+(root/'tests/read-runtime-tail.c').read_text())
PY
cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/battery" "$out/read-runtime.c" -o "$out/read-runtime"
"$out/read-runtime"
