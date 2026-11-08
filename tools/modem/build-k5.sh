#!/bin/sh
# Build only, as build-en2.sh builds sweeten2. Does not contact the phone or change any network setting.
set -eu
here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
out="$root/plans/modem/out"
mkdir -p "$out"
export TMPDIR="$out"
xcrun -sdk iphoneos clang -save-temps=obj -arch arm64e -mios-version-min=18.0 -O2 -Wall -Wextra -Werror "$here/sweetk5.c" -o "$out/sweetk5"
codesign -s - -f "$out/sweetk5"
python3 -I "$root/ios-asteroids/tools/renegade/pacstrip.py" --sign "$out/sweetk5"
