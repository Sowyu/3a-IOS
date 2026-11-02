#!/bin/bash
# Hands-free queue for 2026-11-03 night: waits for the Freeform owner run (pid in $1) to exit, then runs
# lag A, G, A, G (lag agent's GMEM test) and the app spacing v4 test, which also puts the daily driver back.
# Waits on a pid only: run-arms-style queues match their own pattern with pgrep -f.
cd "$(dirname "$0")" || exit 1
while kill -0 "$1" 2>/dev/null; do sleep 10; done
L=$PWD/lag/arms/lumatrace
D=$PWD/lag/next-luma/out-metal
run() { echo "== $(date +%T) start $1"; python3 -u devloop/dev.py "$@"; rc=$?; echo "== $(date +%T) done rc=$rc"; [ $rc -eq 2 ] && { echo "== stopped: dev.py needs attention"; exit 2; }; }
for arm in A60 G60 A60 G60; do
	run lag/out/pace-$arm-boot.img /usr/local/lib/bgmtl/bgmtl-inject.dylib="$L/bgmtl-inject.dylib" \
		/usr/local/lib/bgmtl/libchlorophyll.dylib="$L/libchlorophyll.dylib" --test ui --note pace-$arm
done
run statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/app-safearea/sweetsb-install.dylib" \
	/usr/local/lib/bgmtl/bgmtl-inject.dylib="$D/bgmtl-inject.dylib" /usr/local/lib/bgmtl/libchlorophyll.dylib="$D/libchlorophyll.dylib" \
	--test ui --note app-safearea4 --key 'sweetsb-real'
echo "== all done $(date +%T)"
