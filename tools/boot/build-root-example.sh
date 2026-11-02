#!/bin/bash
# gpu17: every fix so far, captures off (they write about 60 MB per 300 command buffers). Journaled. Does not flash.
cd "$EVERIOS/ios-asteroids"
N=../next-fix/ios26-gpu17-root.simg; I=../next-fix/ios26-gpu17-root.img; J=../next-fix/ios26-gpu17j-root.simg
mount | grep -q research/ios26/full/root && hdiutil detach research/ios26/full/root >/dev/null 2>&1
RENEGADE_TARGET=ios26 RENEGADE_NO_BGMTL=1 RENEGADE_WALLPAPER_POSTER=com.apple.WallpaperKit.CollectionsPoster:7565 RENEGADE_WALLPAPER_FORCE=1 RENEGADE_SWEETNET=$EVERIOS/native-res/sweetnet RENEGADE_CHLOROPHYLL_PERF=1 RENEGADE_CHLOROPHYLL=$EVERIOS/next-fix/out-metal RENEGADE_CHLOROPHYLL_TUDEBUG=sysmem,noubwc,nocb,nolrz RENEGADE_CONSOLE_JOBS=com.apple.pluginkit.pkd,com.apple.containermanagerd,com.apple.PosterBoard RENEGADE_MIN_FREE_GB=10 RENEGADE_SWEETSB=$EVERIOS/plans/statusbar-real/app-safearea/sweetsb.dylib tools/renegade/mkroot-full.sh --size 24g --out "$I" --simg "$N" > research/root-build-gpu17.log 2>&1 || { echo ROOT-FAILED; tail -4 research/root-build-gpu17.log | cut -c1-200; exit 1; }
grep -E ' FAIL ' research/root-build-gpu17.log && { echo VERIFY-FAILED; exit 1; }
grep -n "hide_host_apps\|loop_throttle:\|sweetsb:" research/root-build-gpu17.log | cut -c1-150
h() { python3 -I tools/renegade/hfsx.py $N "$@"; }
for p in "usr/local/lib/bgmtl/bgmtl-inject.dylib ../next-fix/out-metal/bgmtl-inject.dylib" "usr/local/lib/bgmtl/libchlorophyll.dylib ../next-fix/out-metal/libchlorophyll.dylib" "usr/local/lib/sweetsb.dylib ../plans/statusbar-real/app-safearea/sweetsb-install.dylib" "usr/local/bin/sweetuicache research/sweetnet/ios18/sweetuicache"; do set -- $p; h get /$1 /tmp/g17.bin >/dev/null 2>&1; cmp /tmp/g17.bin $2 && echo "match $1" || echo "DIFF $1"; done
h stat /private/var/db/bgmtl/shatter.capture 2>&1 | head -1 | cut -c1-60
python3 tools/renegade/simg2img.py --consume "$N" "$I" || { echo RAW-FAILED; exit 1; }
python3 ../plans/boot/05-enable-journal.py "$I" 2>&1 | tail -1 || { echo JOURNAL-FAILED; exit 1; }
python3 tools/renegade/img2simg.py --consume "$I" "$J" > /dev/null || { echo SIMG-FAILED; exit 1; }
python3 -I tools/renegade/hfsx.py "$J" ls / 2>&1 | grep -c journal
shasum -a 256 "$J" | cut -c1-16; ls -la ../next-fix/*17* | cut -c20-; df -g . | tail -1 | awk '{print "free GiB:",$4}'
