/*
 * Host check for 2153: the real sfs_tick_next() and sfs_latch_lead() (patched SweetFbScale.h) drive a simulated tick against a
 * 120 Hz panel. Present takes 1.0 to 1.4 ms before the flip is written (ast66-70: tick -> hold released 1.1 ms p50, 1.2 to 1.4
 * p90), then the flip latches at the next panel vsync. Starts in the phase the logs show (tick just after a vsync, 7 ms wait),
 * plus random phases and stalls. Asserts the wait settles near the lead, rarely misses, and moves CoreAnimation's tick by at
 * most 1 ms at a time. Times in us.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "SweetFbScale.h"

static int fails;

static double
run(uint32_t lead, double phase0, double panel, int stalls, double *miss_rate, double *maxmove, double *before)
{
	uint64_t deadline = 100000;
	double sum = 0, n = 0, sum0 = 0, n0 = 0, misses = 0;
	*maxmove = 0;
	for (int i = 0; i < 4000; i++) {
		double t = (double)deadline;
		double prep = 1000 + (rand() % 400);
		if (stalls && rand() % 200 == 0) {
			prep += 15000;  /* a slow present now and then */
		}
		double w = t + prep - phase0;
		double latch = phase0 + ceil(w / panel) * panel;
		double wait = latch - t;
		if (i < 20) {
			sum0 += wait;
			n0++;
		}
		if (i >= 200) {
			sum += wait;
			n++;
			misses += wait > prep + panel ? 1 : 0;
		}
		int32_t move = lead ? sfs_latch_lead((uint32_t)wait, lead, 8333, 1000) : 0;
		if (fabs((double)move) > *maxmove) {
			*maxmove = fabs((double)move);
		}
		uint32_t next;
		deadline = sfs_tick_next(deadline + (uint64_t)(int64_t)move, 16667, (uint64_t)(latch + 300), 250, &next);
	}
	*miss_rate = misses / n;
	*before = sum0 / n0;
	return sum / n;
}

int
main(void)
{
	double miss, mv, before;
	srand(3);
	/* today's phase: the tick 1 ms after a vsync */
	double off = run(0, 100000 + 1000 - 8333, 8333.0, 0, &miss, &mv, &before);
	double on = run(3000, 100000 + 1000 - 8333, 8333.0, 0, &miss, &mv, &before);
	printf("tick 1 ms after vsync: wait %.2f ms without, %.2f ms with lead 3 ms (first ticks %.2f), misses %.2f%%, max move %.0f us\n",
	    off / 1000, on / 1000, before / 1000, miss * 100, mv);
	if (on > 3600 || on < 2400) { printf("FAIL: did not settle near the lead\n"); fails++; }
	if (miss > 0.01) { printf("FAIL: misses\n"); fails++; }
	if (mv > 1000) { printf("FAIL: moved more than 1 ms at once\n"); fails++; }
	/* random phases, a panel slightly off 120 Hz, and stalls */
	double worst = 0;
	for (int k = 0; k < 200; k++) {
		double ph = 100000 + (rand() % 8333);
		double panel = 8333.0 + ((rand() % 21) - 10) * 0.05;
		on = run(3000, ph, panel, 1, &miss, &mv, &before);
		if (on > worst) worst = on;
		if (on > 3800 || on < 2300 || miss > 0.02 || mv > 1000) {
			printf("FAIL: phase %.0f panel %.2f: wait %.0f misses %.3f move %.0f\n", ph, panel, on, miss, mv);
			if (++fails > 5) break;
		}
	}
	printf("200 random phases with stalls: worst settled mean wait %.2f ms\n", worst / 1000);
	/* off: exactly the old schedule */
	if (sfs_latch_lead(7000, 3000, 8333, 1000) != 1000 || sfs_latch_lead(3100, 3000, 8333, 1000) != 0 ||
	    sfs_latch_lead(2000, 3000, 8333, 1000) != -1000 || sfs_latch_lead(30000, 3000, 8333, 1000) != 0) {
		printf("FAIL: sfs_latch_lead values\n");
		fails++;
	}
	printf(fails ? "latch_lead_check: %d FAILED\n" : "latch_lead_check: ok\n", fails);
	return fails != 0;
}
