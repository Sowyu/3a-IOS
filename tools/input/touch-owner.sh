# Touch timing with the owner's finger (2026-11-03). dev.py --test ../../../input/selftest/touch-owner
# The kernel's sweettouchirq bit 4 logs one "SWEET TOUCH: touch N:" line per touch (reads, fallback reads, IRQ edges per s,
# noticed to sent). The script never touches the screen for 5 minutes, so the owner's touches are the only ones, then it
# reboots to fastboot as every self-test does.
step owner-touches
say "OWNER: touch now: 5 minutes"
i=0
while [ "$i" -lt 5 ]; do
	sleep 60
	i=$((i + 1))
	shot "owner-$i"
done
say "OWNER: time is up, hands off"
