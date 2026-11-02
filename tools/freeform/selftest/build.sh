#!/bin/bash
# Builds chl-selftest for the phone, linked against next-ff's libchlorophyll and libdrm (found at run time in
# /usr/local/lib/bgmtl). Checks the embedded AIR still matches chl_selftest.ll and that the host translator takes it.
# Touches no phone. Output: selftest/out/chl-selftest, pushed over /usr/local/bin/hcitest (analysis/freeform.md).
set -euo pipefail
cd "$(dirname "$0")"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
lib=../next-ff/out-metal
mkdir -p out
# llvm@22: llvm 23 writes a GUIDLIST module record that the AIR reader refuses
/opt/homebrew/opt/llvm@22/bin/llvm-as chl_selftest.ll -o out/chl_selftest.bc
cmp out/chl_selftest.bc chl_selftest.bc || { echo "chl_selftest.bc is stale: llvm-as it and xxd -i it into chl_selftest_air.h"; exit 1; }
xxd -i chl_selftest.bc | cmp - chl_selftest_air.h || { echo "chl_selftest_air.h is stale"; exit 1; }
../work/host/air2nir2 --report chl_selftest.bc | grep -q 'translate=ok' || { echo "the host translator refuses chl_selftest.bc"; exit 1; }
xcrun --sdk iphoneos clang -target arm64-apple-ios26.0 -O1 -Wall -Wextra -I../next-ff/Chlorophyll/air chl-selftest.c \
	-o out/chl-selftest "$lib/libchlorophyll.dylib" "$lib/libdrm.2.dylib" -Wl,-rpath,/usr/local/lib/bgmtl
codesign --force --sign - out/chl-selftest
otool -L out/chl-selftest | tail -n +2
shasum -a 256 out/chl-selftest
