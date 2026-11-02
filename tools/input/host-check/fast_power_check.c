/*
 * Host check for 2152 v2's state machine, sfs_fast_drain() in the patched SweetFbScale.h: the classification and
 * cancellation rules one by one, then a random walk with the invariants that keep iOS in charge. The timed scenarios from
 * the audit, with the panel thread and frames, are in fast_power_model.c.
 * cc -O1 -Wall -I ../work/s4/iokit/Kernel/arm fast_power_check.c    (run.sh does this)
 */
#include <stdio.h>
#include <stdlib.h>
#include "SweetFbScale.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
enum { W = 2000 };

static uint32_t
one(struct sfs_fast *f, uint32_t mode, uint32_t kind, bool ev_off, uint32_t ev_gen, bool off, uint32_t gen, bool can_wake,
    bool can_blank, uint64_t now)
{
	struct sfs_fast_ev e = { kind, ev_off, ev_gen };
	return sfs_fast_drain(f, mode, &e, kind ? 1 : 0, false, off, gen, can_wake, can_blank, now, W);
}

int
main(void)
{
	struct sfs_fast f = {0};
	uint32_t a;

	/* lock: press with the display on, release in the same generation: dark; iOS's off ends it quietly */
	a = one(&f, 3, SFS_FAST_PRESS, false, 4, false, 4, false, true, 100);
	CHECK(a == 0, "press %u", a);
	a = one(&f, 3, SFS_FAST_SHORT, false, 4, false, 4, false, true, 200);
	CHECK(a == SFS_OP_BLANK && f.dark, "release %u", a);
	a = one(&f, 3, 0, false, 0, true, 5, false, true, 300);
	CHECK(a == 0 && !f.dark, "iOS off %u", a);

	/* audit 2: a wake press whose PRESS is drained after iOS turned on: the snapshot says off, so its release never darkens */
	f = (struct sfs_fast){0};
	a = one(&f, 1, SFS_FAST_PRESS, true, 7, false, 8, false, true, 1000);
	CHECK(a == 0, "late wake press %u", a);
	a = one(&f, 1, SFS_FAST_SHORT, false, 8, false, 8, false, true, 1120);
	CHECK(a == 0 && !f.dark, "release of a wake press darkened (%u)", a);

	/* audit 2 the other way: a lock press drained after iOS already turned off is not a wake */
	f = (struct sfs_fast){0};
	a = one(&f, 3, SFS_FAST_PRESS, false, 9, true, 10, true, true, 2000);
	CHECK(a == 0 && !f.awake, "lock press woke (%u)", a);

	/* iOS changed state between press and release (it acted on the press): no dark */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 11, false, 11, false, true, 3000);
	a = one(&f, 1, SFS_FAST_SHORT, false, 13, false, 13, false, true, 3100);
	CHECK(a == 0, "release after iOS changed state %u", a);

	/* window passes with iOS on: restore, and it does not depend on anything but time */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, true, 4000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, true, 4100);
	a = one(&f, 1, 0, false, 0, false, 1, false, true, 4100 + W - 1);
	CHECK(a == 0 && f.dark, "restored early %u", a);
	a = one(&f, 1, 0, false, 0, false, 1, false, true, 4100 + W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "window end %u", a);

	/* a touch while dark restores at once */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, true, 5000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, true, 5100);
	a = one(&f, 1, SFS_FAST_OTHER, false, 1, false, 1, false, true, 5150);
	CHECK(a == SFS_OP_RESTORE, "touch while dark %u", a);

	/* audit 6: early wake, then a touch or another key while iOS stays off: back to sleep at once */
	f = (struct sfs_fast){0};
	a = one(&f, 2, SFS_FAST_PRESS, true, 2, true, 2, true, true, 6000);
	CHECK(a == SFS_OP_WAKE && f.awake, "wake %u", a);
	a = one(&f, 2, SFS_FAST_OTHER, true, 2, true, 2, false, true, 6200);
	CHECK(a == SFS_OP_SLEEP && !f.awake, "touch after wake %u", a);
	/* and the window ending with iOS still off */
	f = (struct sfs_fast){0};
	one(&f, 2, SFS_FAST_PRESS, true, 2, true, 2, true, true, 7000);
	a = one(&f, 2, 0, false, 0, true, 2, false, true, 7000 + W);
	CHECK(a == SFS_OP_SLEEP, "wake window end %u", a);
	/* iOS's on confirms a wake quietly */
	f = (struct sfs_fast){0};
	one(&f, 2, SFS_FAST_PRESS, true, 2, true, 2, true, true, 8000);
	a = one(&f, 2, 0, false, 0, false, 3, false, true, 8100);
	CHECK(a == 0 && !f.awake, "wake confirmed %u", a);

	/* one batch: dark undone and asked again does not start a second window; press + release together does nothing */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, true, 9000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, true, 9100);
	struct sfs_fast_ev two[2] = { { SFS_FAST_PRESS, false, 1 }, { SFS_FAST_SHORT, false, 1 } };
	a = sfs_fast_drain(&f, 1, two, 2, false, false, 1, false, true, 9300, W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "re-dark in one batch %u", a);
	f = (struct sfs_fast){0};
	a = sfs_fast_drain(&f, 1, two, 2, false, false, 1, false, true, 9500, W);
	CHECK(a == SFS_OP_BLANK, "press+release in one batch from rest %u", a);

	/* a lost event cancels everything */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, true, 10000);
	one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, true, 10100);
	a = sfs_fast_drain(&f, 1, NULL, 0, true, false, 1, false, true, 10200, W);
	CHECK(a == SFS_OP_RESTORE && !f.dark, "lost %u", a);

	/* nothing it cannot undo: no blank without can_blank, no wake without can_wake, mode 0 never acts */
	f = (struct sfs_fast){0};
	one(&f, 1, SFS_FAST_PRESS, false, 1, false, 1, false, false, 11000);
	a = one(&f, 1, SFS_FAST_SHORT, false, 1, false, 1, false, false, 11100);
	CHECK(a == 0, "blank without can_blank %u", a);
	a = one(&f, 2, SFS_FAST_PRESS, true, 1, true, 1, false, true, 11200);
	CHECK(a == 0, "wake without can_wake %u", a);
	struct sfs_fast z = {0};
	for (int i = 0; i < 2000; i++) {
		a = one(&z, 0, (uint32_t)(rand() & 7), rand() & 1, rand() & 3, rand() & 1, rand() & 3, rand() & 1, rand() & 1, i * 37ull);
		CHECK(a == 0, "mode 0 acted %u", a);
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
			snap.off = off;         /* an event's snapshot, maybe older than iOS's state when drained */
			snap.gen = gen;
		}
		struct sfs_fast_ev evs[3];
		unsigned n = (unsigned)(rand() % 4);
		for (unsigned k = 0; k < n; k++) {
			uint32_t kinds[3] = { SFS_FAST_PRESS, SFS_FAST_SHORT, SFS_FAST_OTHER };
			evs[k] = (struct sfs_fast_ev){ kinds[rand() % 3], snap.off, snap.gen };
		}
		bool dark0 = r.dark;
		a = sfs_fast_drain(&r, 3, evs, n, rand() % 500 == 0, off, gen, rand() & 1, rand() & 1, now, W);
		CHECK(!((a & SFS_OP_BLANK) && off), "blank while iOS off");
		CHECK(!((a & SFS_OP_WAKE) && !off), "wake while iOS on");
		CHECK(!((a & SFS_OP_RESTORE) && off), "restore while iOS off");
		CHECK(!(r.dark && r.awake), "dark and awake");
		CHECK(!r.dark || (!off && now < r.until && r.until - now <= W), "dark outlives iOS or its window");
		CHECK(!r.awake || (off && now < r.until && r.until - now <= W), "awake outlives iOS or its window");
		CHECK(!(dark0 && !r.dark && !off) || (a & SFS_OP_RESTORE), "dark ended with iOS on and no restore");
	}
	printf(fails ? "fast_power_check: %d FAILED\n" : "fast_power_check: ok\n", fails);
	return fails != 0;
}
