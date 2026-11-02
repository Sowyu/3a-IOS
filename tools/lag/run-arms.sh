#!/bin/bash
# Alternating A/B runs through dev.py, one at a time. Usage: run-arms.sh NOTE:IMAGE:ARMDIR ...
cd "$(dirname "$0")/.." || exit 1
for spec in "$@"; do
	IFS=: read -r note img arm <<< "$spec"
	while pgrep -f devloop/dev.py > /dev/null; do sleep 10; done
	echo "== $(date +%T) start $note $img $arm"
	python3 -u devloop/dev.py "$img" /usr/local/lib/bgmtl/bgmtl-inject.dylib="$PWD/lag/arms/$arm/bgmtl-inject.dylib" \
		/usr/local/lib/bgmtl/libchlorophyll.dylib="$PWD/lag/arms/$arm/libchlorophyll.dylib" --test ui --note "$note"
	rc=$?
	echo "== $(date +%T) done $note rc=$rc"
	[ $rc -eq 2 ] && { echo "== stopped: dev.py needs attention"; exit 2; }
done
echo "== all done"
