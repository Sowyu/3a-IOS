#!/bin/sh
# The host checks, on the staged sources (work/s2 for touch, work/s4 for the header). Scratch dir is trashed after.
set -eu
h=$(cd "$(dirname "$0")" && pwd)
t=$h/../work/tmp-check
mkdir -p "$t"
python3 -I "$h/touch_pace_check.py" "$h/../work/s2/bsd/dev/sweet_touch_asteroids.c"
cc -O1 -Wall -I "$h/../work/s4/iokit/Kernel/arm" "$h/fast_power_check.c" -o "$t/fast_power_check"
"$t/fast_power_check"
cc -O1 -Wall -I "$h/../work/s4/iokit/Kernel/arm" "$h/latch_lead_check.c" -o "$t/latch_lead_check" -lm
"$t/latch_lead_check"
python3 -I "$h/fast_power_model.py" 3000
/usr/bin/trash "$t"
