#!/bin/sh
# 2150, 2151, 2153 and 2152 v3 are fixed (they are in the owner's build); this script never rewrites them, it checks their
# hashes. It builds 2152 v4 (a replacement for v3, not on top of it) from work/p3 (2120 series + 2150 + 2151 + 2153) to
# work/p5, then checks on scratch
# copies of the tree's current files, never the tree itself:
#   2120: the series through 2120 (upstream, 2043-2049, 2051-2120), made from the live files by reverse-applying the lag
#         agent's 214x patches (plans/lag/patches), newest first, when present. That is how work/s0 was made.
#   live: the files exactly as they are, with whatever the lag agent has applied.
# On each: 2150, 2151, 2153, then 2152 v3, and separately 2152 v4, on top.
set -eu
H=$(cd "$(dirname "$0")/.." && pwd)
W=$H/work
X=$(cd "$H/../../ios-asteroids/research/xnu-local/dxb/xnu" && pwd)
L=$(cd "$H/../lag/patches" && pwd)
shasum -a 256 -c - <<SUMS
ca6a773479933848dd95b7c517d9dbabb0be7e8715efc48137627c200d2cb02d  $H/patches/2150-asteroids-input-timing-keys.patch
1badb7d7907df76294eb60d217b6586f388edefc9cb524e9f746d02fc6414d31  $H/patches/2151-asteroids-touch-irq-pace.patch
e199401666c882b7ddfc6a69d58836100e0dd8851e187972880cc5acb08066a0  $H/patches/2153-asteroids-latch-lead.patch
d335e9bdf14a46379962bcdd0395492a5430a31ecbf580420e27f2424a3f6973  $H/patches/2152-asteroids-fast-power.patch
SUMS
out=$H/patches/2152-asteroids-fast-power-v4.patch
: > "$out"
(cd "$W/p5" && find . -type f | sed 's|^\./||' | sort) | while read -r f; do
	if ! cmp -s "$W/p3/$f" "$W/p5/$f"; then
		diff -u --label "a/$f" --label "b/$f" "$W/p3/$f" "$W/p5/$f" >> "$out" || true
	fi
done
echo "2152 v4: $(grep -c '^@@' "$out") hunks, sha256 $(shasum -a 256 "$out" | cut -d' ' -f1)"
files=$( (cd "$W/s0" && find . -type f | sed 's|^\./||'); for p in "$L"/214[0-9]-*.patch; do sed -n 's|^+++ b/||p' "$p"; done)
files=$(echo "$files" | sort -u)
for base in 2120 live; do
	S=$(mktemp -d "$W/scratch-$base-XXXXXX")
	for f in $files; do
		mkdir -p "$S/$(dirname "$f")"
		cp "$X/$f" "$S/$f"
	done
	cd "$S"
	if [ $base = 2120 ]; then
		for p in $(ls "$L"/214[0-9]-*.patch | sort -r); do
			if git apply -R --check "$p" 2> /dev/null; then
				git apply -R "$p" && echo "2120 base: reversed lag $(basename "$p" | cut -c1-4)"
			fi
		done
		for f in $(cd "$W/s0" && find . -type f | sed 's|^\./||'); do
			cmp -s "$S/$f" "$W/s0/$f" || echo "NOTE: the 2120 base differs from work/s0 in $f"
		done
	else
		echo "live tree: lag lines present: $(grep -c '/\* 214[0-9]' iokit/Kernel/arm/SweetFramebuffer.cpp)"
	fi
	for n in 2150 2151 2153; do
		git apply "$H"/patches/$n-*.patch && echo "$base: applies in order: $n" || echo "$base: FAILS: $n"
	done
	for v in "" -v4; do
		git apply --check "$H/patches/2152-asteroids-fast-power$v.patch" && echo "$base: then 2152$v applies" || echo "$base: FAILS: 2152$v"
	done
	cd "$H"
	/usr/bin/trash "$S"
done
cd "$X"
git apply --check "$H"/patches/2150-*.patch && echo "git apply --check in the tree: 2150 ok"
