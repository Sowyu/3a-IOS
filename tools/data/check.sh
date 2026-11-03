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
	for f in QmiData.c QmiData.h Qmap.c Qmap.h CellLink.c CellLink.h CellSession.c CellSession.h QmiRaw.c QmiRaw.h DataServices.c DataServices.h; do
		python3 -I "$root/../modem/extract.py" "$root/../../patches/data" "$src/$f" "bsd/dev/data/$f"
	done
fi
cc -std=gnu11 -DMODEM_PROTOCOL_HOST_TEST -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" -I"$src" "$root/tests/data.c" "$src/QmiData.c" "$src/Qmap.c" \
	"$src/CellLink.c" "$src/CellSession.c" "$src/QmiRaw.c" "$out/QcomQrtr.c" -o "$out/check-data"
# modem 2189 audit finding 1: the real service cache between real QRTR and the real session
cc -std=gnu11 -DMODEM_PROTOCOL_HOST_TEST -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out" -I"$src" "$root/tests/services.c" \
	"$src/DataServices.c" "$src/CellSession.c" "$src/QmiData.c" "$out/QcomQrtr.c" -o "$out/check-services"
"$out/check-services"
"$out/check-data"
# 2221: the IPA adapter against stubs of 2220's hooks and 2202's API
t="$out/ipa"; mkdir -p "$t/bsd/dev/data" "$t/bsd/dev/modem" "$t/inc/libkern"
printf '#include <stdio.h>\n' > "$t/inc/libkern/libkern.h"
python3 -I "$root/../modem/extract.py" "$root/../../patches/ipa" "$t/bsd/dev/modem/SweetIpa.h" bsd/dev/modem/SweetIpa.h
cp "$out/QcomQrtr.h" "$t/bsd/dev/modem/"
for f in sweet_data_ipa.c sweet_data_ipa.h sweet_data.h; do
	if [ -n "${DATA_SRC2221:-}" ] && [ -e "$DATA_SRC2221/$f" ]; then cp "$DATA_SRC2221/$f" "$t/bsd/dev/data/"
	elif [ -n "${DATA_SRC:-}" ] && [ -e "$DATA_SRC/$f" ]; then cp "$DATA_SRC/$f" "$t/bsd/dev/data/"
	else python3 -I "$root/../modem/extract.py" "$root/../../patches/data" "$t/bsd/dev/data/$f" "bsd/dev/data/$f"; fi
done
cp "$root/tests/ipa.c" "$t/bsd/dev/data/ipa-test.c"
cc -std=gnu11 -DARM64_BOARD_CONFIG_SWEET -Wall -Wextra -Werror -fsanitize=address,undefined -I"$t/inc" -I"$t/bsd/dev/data" \
	"$t/bsd/dev/data/ipa-test.c" "$t/bsd/dev/data/sweet_data_ipa.c" -o "$out/check-ipa"
"$out/check-ipa"
