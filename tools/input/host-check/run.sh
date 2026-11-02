#!/bin/sh
# The host checks, on the staged sources: work/p2 for touch (2151), work/p5 (2152 v4) for 2152 and 2153's header.
# Scratch files go under plans/input/work and are moved to the Trash with /usr/bin/trash, never deleted.
set -eu
h=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d "$h/../work/tmp-check-XXXXXX")
python3 -I "$h/touch_pace_check.py" "$h/../work/p2/bsd/dev/sweet_touch_asteroids.c"
cc -O1 -Wall -I "$h/../work/p5/iokit/Kernel/arm" "$h/fast_power_check.c" -o "$t/fast_power_check"
"$t/fast_power_check"
cc -O1 -Wall -I "$h/../work/p5/iokit/Kernel/arm" "$h/latch_lead_check.c" -o "$t/latch_lead_check" -lm
"$t/latch_lead_check"
python3 -I "$h/fast_power_model.py" 3000
python3 -I "$h/patch_scope_check.py"
python3 -I "$h/patch_scope_check.py" "$h/../patches/2152-asteroids-fast-power-v4.patch" "$h/../work/p5/iokit/Kernel/arm/SweetFramebuffer.cpp"
/usr/bin/trash "$t"
