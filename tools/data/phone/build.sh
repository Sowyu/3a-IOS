#!/bin/sh
# Builds sweetdhcp for the iOS 26 root: arm64e, ad-hoc signed, then pacstrip (the kernel has no pointer authentication).
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
out=${OUT:-$here/out}
pacstrip=${PACSTRIP:-$here/../../../../ios-asteroids/tools/renegade/pacstrip.py}
mkdir -p "$out"
xcrun -sdk iphoneos clang -arch arm64e -mios-version-min=18.0 -O2 -Wall -fobjc-arc -framework Foundation \
	"$here/sweetdhcp.m" -o "$out/sweetdhcp"
codesign -s - -f --entitlements "$here/sweetdhcp.entitlements" "$out/sweetdhcp"
python3 -I "$pacstrip" --sign "$out/sweetdhcp"
ls -l "$out/sweetdhcp"
