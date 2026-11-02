/*
 * Host check for 2152 v3's pure parts in the patched SweetFbScale.h: sfs_fast_drain (classification, cancellation, the
 * window) and sfs_fast_keep_next (the debt of the kept image), rule by rule, then a random walk. The timed scenarios with
 * flips, fallback and surface holds are in fast_power_model.c.
 * cc -O1 -Wall -I ../work/p4/iokit/Kernel/arm fast_power_check.c    (run.sh does this)
 */
#include <stdio.h>
#include <stdlib.h>
#include "SweetFbScale.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
enum { W = 2000 };

static uint32_t
one(struct sfs_fast *f, uint32_t mode, uint32_t kind, bool ev_off, uint32_t ev_gen, bool off, uint32_t gen, bool can_blank,
    uint64_t now)
{
	struct sfs_fast_ev e = { kind, ev_off, ev_gen };
	return sfs_fast_drain(f, mode, &e, kind ? 1 : 0, false, off, gen, can_blank, now, W);
}

int
main(void)
{
	struct sfs_fast f = {0};
	uint32_t a;

	/* lock: press with the display on, release in the same generation: black; iOS's off ends it with nothing to restore */
	a = one(&f, 1, SFS_FAST_PRESS, false, 4, false, 4, true, 100);
	CHECK(a == 0, "press %u", a);
	a = one(&f, 1, SFS_FAST_SHORT, false, 4, false, 4, true, 200);
	CHECK(a == SFS_OP_BLANK && f.dark, "release %u", a);
	a = one(&f, 1, 0, false, 0, true, 5, true, 300);
	CHECK(a == 0 && !f.dark, "iOS off %u", a);

	/* a wake press drained after iOS turned on: the snapshot says off, so its release never darkens */
	f = (struct sfs_fast){0};
	a = one(&f, 1, SFS_FAST_PRESS, true, 7, false, 8, true, 1000);
	a |= one(&f, 1, SFS_FAST_SHORT, false, 8, false, 8, true, 1120);
	CHECK(a == 0 && !f.dark, "release of a wake press darkened (%u)", a);

	/* iOS changed state between press and release (it acted on the press): no dark */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 11, false, 11, true, 3000);
	a = one(&f, 1, SFS_FAST_SHORT, false, 13, false, 13, true, 3100);
	CHECK(a == 0, "release after iOS changed state %u", a);

	/* the window passes with iOS on: restore, on time alone */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, true, 4000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, true, 4100);
	a = one(&f, 1, 0, false, 0, false, 1, true, 4100 + W - 1);
	CHECK(a == 0 && f.dark, "restored early %u", a);
	a = one(&f, 1, 0, false, 0, false, 1, true, 4100 + W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "window end %u", a);

	/* a touch while dark restores at once */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, true, 5000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, true, 5100);
	a = one(&f, 1, SFS_FAST_OTHER, false, 1, false, 1, true, 5150);
	CHECK(a == SFS_OP_RESTORE, "touch while dark %u", a);

	/* one batch: undone and asked again starts no second window; press + release together from rest is one short press */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, true, 9000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, true, 9100);
	struct sfs_fast_ev two[2] = { { SFS_FAST_PRESS, false, 1 }, { SFS_FAST_SHORT, false, 1 } };
	a = sfs_fast_drain(&f, 1, two, 2, false, false, 1, true, 9300, W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "re-dark in one batch %u", a);
	f = (struct sfs_fast){0};
	a = sfs_fast_drain(&f, 1, two, 2, false, false, 1, true, 9500, W);
	CHECK(a == SFS_OP_BLANK, "press+release in one batch from rest %u", a);

	/* a lost event ends a dark step */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, true, 10000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, true, 10100);
	a = sfs_fast_drain(&f, 1, NULL, 0, true, false, 1, true, 10200, W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "lost %u", a);

	/* no blank without can_blank (a debt still owed, a stopped flip path); mode 0 never acts; bit 2 means nothing now */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, 11000);
	a = one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, 11100);
	CHECK(a == 0, "blank without can_blank %u", a);
	f = (struct sfs_fast){0};
	one(&f, 2, SFS_FAST_PRESS, false, 1, false, 1, true, 11200);
	a = one(&f, 2, SFS_FAST_SHORT, false, 1, false, 1, true, 11300);
	CHECK(a == 0, "mode 2 acted %u", a);
	struct sfs_fast z = {0};
	for (int i = 0; i < 2000; i++) {
		a = one(&z, 0, (uint32_t)(rand() & 7), rand() & 1, rand() & 3, rand() & 1, rand() & 3, rand() & 1, i * 37ull);
		CHECK(a == 0, "mode 0 acted %u", a);
	}

	/* sfs_fast_keep_next: args ios_off, stopped, frozen, kept_on_screen, boot_written */
	CHECK(sfs_fast_keep_next(true, true, true, false, false) == SFS_KEEP_DONE, "iOS off pays");
	CHECK(sfs_fast_keep_next(false, false, false, false, false) == SFS_KEEP_FLIP, "normal: flip back");
	CHECK(sfs_fast_keep_next(false, false, false, true, false) == SFS_KEEP_DONE, "flip back latched: paid");
	CHECK(sfs_fast_keep_next(false, true, false, false, false) == SFS_KEEP_BOOT, "stopped: boot buffer");
	CHECK(sfs_fast_keep_next(false, true, false, true, false) == SFS_KEEP_BOOT, "stopped: kept_on_screen means nothing");
	CHECK(sfs_fast_keep_next(false, true, false, false, true) == SFS_KEEP_DONE, "stopped, boot written, scanned: paid");
	CHECK(sfs_fast_keep_next(false, true, true, false, false) == SFS_KEEP_BOOT, "frozen: still write the boot buffer");
	CHECK(sfs_fast_keep_next(false, true, true, false, true) == SFS_KEEP_WAIT, "frozen, written: the debt stays");
	/* never paid with iOS on unless the image is on the screen: exhaustive */
	for (int m = 0; m < 32; m++) {
		bool off = m & 1, st = m & 2, fr = m & 4, on = m & 8, bw = m & 16;
		int k = sfs_fast_keep_next(off, st, fr, on, bw);
		bool shown = st ? (bw && !fr) : on;
		CHECK(!(k == SFS_KEEP_DONE && !off && !shown), "paid without the image: off %d stopped %d frozen %d on %d boot %d", off,
		    st, fr, on, bw);
	}

	/* random walk: iOS changes on its own, events arrive late with old snapshots, batches of 0..3 */
	srand(7);
	struct sfs_fast r = {0};
	bool off = false;
	uint32_t gen = 0;
	uint64_t now = 0;
	struct { bool off; uint32_t gen; } snap = { false, 0 };
	for (int i = 0; i < 2000000 && fails < 5; i++) {
		now += (uint64_t)(rand() % 120);
		if (rand() % 10 == 0) {
			off = !off;
			gen++;
		}
		if (rand() % 3 == 0) {
			snap.off = off;
			snap.gen = gen;
		}
		struct sfs_fast_ev evs[3];
		unsigned n = (unsigned)(rand() % 4);
		for (unsigned k = 0; k < n; k++) {
			uint32_t kinds[3] = { SFS_FAST_PRESS, SFS_FAST_SHORT, SFS_FAST_OTHER };
			evs[k] = (struct sfs_fast_ev){ kinds[rand() % 3], snap.off, snap.gen };
		}
		bool dark0 = r.dark;
		a = sfs_fast_drain(&r, 1, evs, n, rand() % 500 == 0, off, gen, rand() & 1, now, W);
		CHECK(!((a & SFS_OP_BLANK) && off), "blank while iOS off");
		CHECK(!((a & SFS_OP_RESTORE) && off), "restore while iOS off");
		CHECK(!r.dark || (!off && now < r.until && r.until - now <= W), "dark outlives iOS or its window");
		CHECK(!(dark0 && !r.dark && !off) || (a & SFS_OP_RESTORE), "dark ended with iOS on and no restore");
	}
	printf(fails ? "fast_power_check: %d FAILED\n" : "fast_power_check: ok\n", fails);
	return fails != 0;
}
