#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out="$here/tests/out"
mkdir -p "$out"
export TMPDIR=/tmp
for name in IpaMmio.h IpaFirmware.h SweetIpa.h IpaGsi.h IpaLayout.h IpaQmi.h IpaHardware.inc IpaTxQueue.h IpaData.inc IpaSession.inc; do
    python3 "$here/../modem/extract.py" "$here/../../patches/ipa" "$out/$name" "bsd/dev/modem/$name"
done
for name in QcomQrtr.h QcomQrtr.c QcomSmp2p.h QcomSmp2p.c; do
    python3 "$here/../modem/extract.py" "$here/../../patches/modem" "$out/$name" "bsd/dev/modem/$name"
done
for name in gsi layout tx qmi hardware; do
    set --
    case "$name" in
        qmi) set -- "$out/QcomQrtr.c" ;;
        hardware) set -- "$out/QcomQrtr.c" "$out/QcomSmp2p.c" ;;
    esac
    cc -save-temps=obj -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -DMODEM_PROTOCOL_HOST_TEST \
        -I"$out" "$here/tests/$name.c" "$@" -o "$out/$name"
    ASAN_OPTIONS=detect_leaks=0 "$out/$name"
done
