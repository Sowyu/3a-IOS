#!/usr/bin/env python3
"""Host check for 2151: compiles the driver's real at_read_due() (cut from the patched sweet_touch_asteroids.c) into a
simulation of the poll loop against a controller that pulses its IRQ once per report, and compares the old policy
(read every pass while touched, 2 ms wait) with pace mode (read on the latched edge, 0.5 ms passes, 12 ms fallback).
Asserts that pace mode sends every report, sooner, with steadier timestamps, and that a lost edge is read by the fallback.
Run: python3 -I touch_pace_check.py [path to sweet_touch_asteroids.c]. Times are a model: the read cost (0.9 ms) is the
93-byte frame at about 1 MHz plus the 150 us CS delay; report rates 120 and 240 Hz are guesses, the stats line measures it."""
import os, pathlib, re, subprocess, sys, tempfile
here = pathlib.Path(__file__).resolve().parent
src = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent / 'work/p2/bsd/dev/sweet_touch_asteroids.c'
text = src.read_text()
m = re.search(r'\nstatic bool\nat_read_due\(.*?\n}\n', text, re.S)
assert m, 'at_read_due not found'
fn = m.group(0)
harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
''' + fn + r'''
static int g_reads;
/* times in us; one simulated second of touch. last_lost >= 0: reports stop after that one (a lift's UP record) and its IRQ
 * edge is lost; drop_gap is how long after it was ready the driver sent it. */
static int run(bool pace, double period, double read_us, double wait_us, double pass_us, int last_lost, int stamp_at_seen,
    double *delay_avg, double *jitter, int *sent, int *reports, double *drop_gap)
{
	double t = 0.37, last_check = 0, last_read = -1e9, last_stamp = -1, sum = 0, sq = 0, dsum = 0;
	int newest_sent = -1, nd = 0, n = last_lost >= 0 ? last_lost + 1 : (int)(1e6 / period);
	double end = n * period + 40000;
	unsigned ticks = 0;
	*sent = 0; *reports = n; *drop_gap = -1;
	while (t < end) {
		int k0 = (int)floor(last_check / period) + 1, k1 = (int)floor(t / period);
		bool edge = false;
		for (int k = k0; k <= k1 && k < n; k++) {
			if (k != last_lost) edge = true;
		}
		last_check = t;
		if (at_read_due(pace, true, false, edge, (uint64_t)(t - last_read), 12000, &ticks, 25)) {
			last_read = t;
			g_reads++;
			int k = (int)floor(t / period);
			if (k >= n) k = n - 1;
			double done = t + read_us;
			if (k >= 0 && k > newest_sent) {
				if (k == last_lost) *drop_gap = done - k * period;
				double stamp = stamp_at_seen ? t : done;
				sum += done - k * period; nd++;
				if (last_stamp >= 0) {
					double dt = stamp - last_stamp;
					dsum += dt; sq += dt * dt;
				}
				last_stamp = stamp;
				newest_sent = k;
				(*sent)++;
			}
			t = done + wait_us;
		} else {
			t += pass_us;
		}
	}
	*delay_avg = nd ? sum / nd : 0;
	int m = *sent - 1;
	*jitter = m > 1 ? sqrt(sq / m - (dsum / m) * (dsum / m)) : 0;
	return 0;
}
int main(void)
{
	int fails = 0;
	double periods[2] = {8333.3, 4166.7};
	for (int p = 0; p < 2; p++) {
		double d0, j0, d1, j1, d2, j2, g;
		int s0, s1, s2, r, r2;
		/* old: every pass reads while touched, then waits 2 ms; events stamped when sent */
		run(false, periods[p], 900, 2000, 2000, -1, 0, &d0, &j0, &s0, &r, &g);
		/* pace: read on the edge, 0.5 ms passes; stamped at the moment the edge was noticed */
		g_reads = 0;
		run(true, periods[p], 900, 500, 500, -1, 1, &d1, &j1, &s1, &r, &g);
		int pace_reads = g_reads;
		/* pace with one lost edge: the fallback must read it */
		run(true, periods[p], 900, 500, 500, 60, 1, &d2, &j2, &s2, &r2, &g);
		printf("%5.0f Hz reports=%d | old: sent %d, report->sent %.2f ms, stamp jitter %.2f ms | pace: sent %d, %.2f ms, jitter %.2f ms | lost last edge read after %.1f ms\n",
		    1e6 / periods[p], r, s0, d0 / 1000, j0 / 1000, s1, d1 / 1000, j1 / 1000, g / 1000);
		if (pace_reads > r + r / 10) { printf("FAIL: pace mode read %d times for %d reports: not on the IRQ\n", pace_reads, r); fails++; }
		if (s1 < r - 1) { printf("FAIL: pace mode skipped reports\n"); fails++; }
		if (d1 >= d0) { printf("FAIL: pace mode is not sooner\n"); fails++; }
		if (j1 >= j0 || j1 > 600) { printf("FAIL: pace stamps not steadier\n"); fails++; }
		if (g <= 0 || g > 12000 + periods[p] + 1500) { printf("FAIL: lost edge not recovered by the fallback (%.0f us)\n", g); fails++; }
	}
	/* idle passes keep the old 100 ms periodic read */
	unsigned ticks = 0, reads = 0;
	for (int i = 0; i < 100; i++) reads += at_read_due(true, false, false, false, 0, 12000, &ticks, 25);
	if (reads != 4) { printf("FAIL: idle periodic read count %u, want 4\n", reads); fails++; }
	if (!at_read_due(true, false, true, false, 0, 12000, &ticks, 25)) { printf("FAIL: IRQ low while idle must read\n"); fails++; }
	if (!at_read_due(false, true, false, false, 0, 12000, &ticks, 25)) { printf("FAIL: default mode must read every pass while touched\n"); fails++; }
	printf(fails ? "touch_pace_check: %d FAILED\n" : "touch_pace_check: ok\n", fails);
	return fails != 0;
}
'''
d = tempfile.mkdtemp(prefix='tmp-touch-', dir=here.parent / 'work')     # moved to the Trash below, never deleted
try:
    c, exe = os.path.join(d, 'sim.c'), os.path.join(d, 'sim')
    open(c, 'w').write(harness)
    subprocess.run(['cc', '-O1', '-Wall', '-o', exe, c, '-lm'], check=True)
    rc = subprocess.run([exe]).returncode
finally:
    subprocess.run(['/usr/bin/trash', d], check=True)
sys.exit(rc)
