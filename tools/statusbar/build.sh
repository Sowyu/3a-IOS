#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
export TMPDIR="$PWD/tmp"
mkdir -p "$TMPDIR"
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
artifact_dir=.
feature_flags=()
case "${1:-}" in
  '') ;;
  safearea) artifact_dir=app-safearea; feature_flags=(-DSWEETSB_APP_SAFEAREA=1 -DSWEETSB_HOME_SHIFT=1); mkdir -p "$artifact_dir" ;;
  *) echo 'usage: build.sh [safearea]' >&2; exit 2 ;;
esac
xcrun --sdk iphoneos clang -target arm64e-apple-ios26.0 -isysroot "$SDK" -O1 -fno-objc-arc -Wall -Wextra -Wno-unused-parameter "${feature_flags[@]}" -dynamiclib -framework Foundation -framework CoreGraphics -lobjc -install_name /usr/local/lib/sweetsb.dylib sweetsb.m -o "$artifact_dir/sweetsb.dylib"
codesign --force --sign - "$artifact_dir/sweetsb.dylib"
# The file-only route needs the same PAC rewrite the root builder performs.
cp "$artifact_dir/sweetsb.dylib" "$artifact_dir/sweetsb-install.dylib"
PYTHONDONTWRITEBYTECODE=1 python3 -B ../../ios-asteroids/tools/renegade/pacstrip.py --sign "$artifact_dir/sweetsb-install.dylib"
codesign --verify --strict "$artifact_dir/sweetsb-install.dylib"
shasum -a 256 "$artifact_dir"/sweetsb*.dylib
