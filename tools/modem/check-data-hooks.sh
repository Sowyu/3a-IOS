#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
for file in ModemRuntime.inc ModemGate.h ModemUim.c ModemUim.h QcomQrtr.c QcomQrtr.h QcomGlink.h; do
 python3 "$root/extract.py" "$root/../../patches/modem" "$out/$file" "bsd/dev/modem/$file"
done
for file in CellSession.c CellSession.h QmiData.c QmiData.h Qmap.h DataServices.c DataServices.h sweet_data.c; do
 python3 "$root/extract.py" "$root/../../patches/data" "$out/$file" "bsd/dev/data/$file"
done
for name in worker gates readiness freshness discovery; do
 python3 "$root/check-data-$name.py"
done
for name in worker gates; do
 cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" "$out/$name.c" -o "$out/check-data-$name"
 "$out/check-data-$name"
done
cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$out/readiness.c" "$out/ModemUim.c" "$out/QcomQrtr.c" -o "$out/check-data-readiness"
"$out/check-data-readiness"

for name in freshness discovery; do
 cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -DMODEM_PROTOCOL_HOST_TEST -fsanitize=address,undefined -I"$out" "$out/$name.c" "$out/CellSession.c" "$out/QmiData.c" "$out/DataServices.c" "$out/QcomQrtr.c" -o "$out/check-data-$name"
 "$out/check-data-$name"
done
