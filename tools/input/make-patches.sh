#!/bin/sh
# Rebuild plans/input/patches/215x-*.patch from the staged copies in plans/input/work/s0..s4, then check the series in order
# on two scratch copies of the tree's current files (never the tree itself):
#   2120: the kernel series through 2120 (3a-IOS/patches/kernel). The lag agent's 2142, 2141 and 2140 are reverse-applied
#         from the live files when present, which is how work/s0 was made.
#   live: the files exactly as they are now, with whatever the lag agent has applied.
# Plus git apply --check of 2150 alone in the tree.
set -eu
H=$(cd "$(dirname "$0")/.." && pwd)
W=$H/work
X=$(cd "$H/../../ios-asteroids/research/xnu-local/dxb/xnu" && pwd)
L=$(cd "$H/../lag/patches" && pwd)
names="2150-asteroids-input-timing-keys 2151-asteroids-touch-irq-pace 2152-asteroids-fast-power 2153-asteroids-latch-lead"
i=0
for n in $names; do
	a=s$i; b=s$((i + 1)); i=$((i + 1))
	[ -d "$W/$b" ] || continue
	out=$H/patches/$n.patch
	: > "$out"
	(cd "$W/$b" && find . -type f | sed 's|^\./||' | sort) | while read -r f; do
		if ! cmp -s "$W/$a/$f" "$W/$b/$f"; then
			diff -u --label "a/$f" --label "b/$f" "$W/$a/$f" "$W/$b/$f" >> "$out" || true
		fi
	done
	echo "$n: $(grep -c '^@@' "$out") hunks"
done
# the files the input patches or the lag patches touch
files=$( (cd "$W/s0" && find . -type f | sed 's|^\./||'); for p in "$L"/214[0-2]-*.patch; do sed -n 's|^+++ b/||p' "$p"; done)
files=$(echo "$files" | sort -u)
for base in 2120 live; do
	S=$W/scratch-$base
	[ -d "$S" ] && /usr/bin/trash "$S"
	mkdir -p "$S"
	for f in $files; do
		mkdir -p "$S/$(dirname "$f")"
		cp "$X/$f" "$S/$f"
	done
	cd "$S"
	if [ $base = 2120 ]; then
		for p in 2142 2141 2140; do
			if git apply -R --check "$L/$p"-*.patch 2> /dev/null; then
				git apply -R "$L/$p"-*.patch && echo "2120 base: reversed lag $p"
			fi
		done
		for f in $(cd "$W/s0" && find . -type f | sed 's|^\./||'); do
			cmp -s "$S/$f" "$W/s0/$f" || echo "NOTE: the 2120 base differs from work/s0 in $f"
		done
	else
		echo "live tree: lag lines present: $(grep -c '/\* 214[0-2]' iokit/Kernel/arm/SweetFramebuffer.cpp)"
	fi
	for p in "$H"/patches/215*.patch; do
		git apply "$p" && echo "$base: applies in order: $(basename "$p")" || echo "$base: FAILS: $(basename "$p")"
	done
	cd "$H"
	/usr/bin/trash "$S"
done
cd "$X"
git apply --check "$H"/patches/2150-*.patch && echo "git apply --check in the tree: 2150 ok"
