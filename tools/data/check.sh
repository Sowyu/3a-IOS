#!/bin/sh
# Host checks for the data patches (2220+): QMI layouts, QMAP, the Ethernet presentation, the session order, the raw allow list.
# Sources come from the public patches: the modem's QcomQrtr from patches/modem, the data code from patches/data (or $DATA_SRC).
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$root/tests/out"
mkdir -p "$out/tmp"
export TMPDIR="$out/tmp"
for f in QcomQrtr.c QcomQrtr.h; do python3 -I "$root/../modem/extract.py" "$root/../../patches/modem" "$out/$f" "bsd/dev/modem/$f"; done
src=${DATA_SRC:-}
if [ -z "$src" ]; then
	src="$out/data"; mkdir -p "$src"
	for f in QmiData.c QmiData.h Qmap.c Qmap.h CellLink.c CellLink.h CellSession.c CellSession.h QmiRaw.c QmiRaw.h; do
		python3 -I "$root/../modem/extract.py" "$root/../../patches/data" "$src/$f" "bsd/dev/data/$f"
	done
fi
cc -std=gnu11 -DMODEM_PROTOCOL_HOST_TEST -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" -I"$src" "$root/tests/data.c" "$src/QmiData.c" "$src/Qmap.c" \
	"$src/CellLink.c" "$src/CellSession.c" "$src/QmiRaw.c" "$out/QcomQrtr.c" -o "$out/check-data"
"$out/check-data"
