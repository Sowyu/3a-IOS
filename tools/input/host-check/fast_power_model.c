/*
 * 2152 regression model, pixel path (sweetpanel=0, asteroids flip). Built three times by fast_power_model.py:
 *   -DOLD       v2 as Sol audited it (analysis/2152-reaudit.md): v2's SweetFbScale.h and v2's pixel glue.
 *   -DNEW       v3: its SweetFbScale.h, whose sfs_fast_drain and sfs_fast_keep_next are the real code, and a line-by-line
 *               mirror of v3's fastRun, fastRestore, fastShowInBoot and the vsync tick's no-flip present.
 *   -DNEW -DV4  v4: frames taken while black are composed in a stage, never the kept slot; the stage counts as complete
 *               only when present reaches astFlip (whole frame, epoch current); records with the default surface are
 *               refused while black (analysis/2152-audit3.md finding 2).
 * Records: NORMAL, ABORT (a later layer is the default surface: with hardware scale present calls astFallback after the
 * first layer was written), EPOCH (a GPU recovery during the copy: present's epoch check refuses it after the writes).
 * The display is modelled from SweetFramebuffer.cpp: three buffers (0 = boot), astFlip with latch failures, astFallback
 * (stops flipping, seeds the boot buffer from the front or, with hardware scale, black, then restores scanout to it or
 * freezes), copy mode once stopped, the GPU gate and epochs, CoreAnimation surface holds with and without sweetfb_keep, and
 * iOS's power requests. 0.25 ms steps.
 *
 * Mutation switches for the NEW glue mirror (-DMUT_...) take each v3 fix out again; fast_power_model.py checks that each one
 * makes a scenario fail. Mutations of the real header functions are done on copies of the header.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SweetFbScale.h"

typedef long long T;
#define MS 1000LL
#define STEP 250LL
#define TICK 16667LL
#define WINDOW (2000 * MS)
#define BLACK (-1)
#define UNKNOWN (-2)
#define PARTIAL (-3)            /* some layers written, then present stopped */
#define INVALID (-4)            /* written, then refused by the epoch check */

enum { E_PRESS, E_RELEASE, E_TOUCH, E_GPU_RECOVERY, E_SWAPS_OFF, E_IOS_OFF, E_ABORT_NEXT, E_EPOCH_NEXT, E_END };
enum { R_NORMAL, R_ABORT, R_EPOCH };
struct sev { T t; int what; };
struct scen {
	const char *name;
	bool hw, keep;
	T consume, wake_on_down, lock_on_release;       /* iOS's reaction delays, -1 = none */
	int fail_flip[4];               /* these flip attempts (1-based, counted from fail_after) do not latch */
	T fail_after;
	bool fail_restore;              /* astFallback cannot put scanout back on the boot buffer: frozen */
	struct sev ev[12];
	T end;
};

static struct {
	T now;
	const struct scen *s;
	bool ios_off; uint32_t gen; bool ios_pending, ios_to; T ios_at;
	int buf[3], front; bool stopped, frozen, black_done;
	uint32_t epoch, pend_epoch; int pend_frame, pend_kind, next_kind; bool pend_valid, dirty, swaps; T next_swap; int frame_no;
	int stage; bool stage_valid;    /* v4 */
	T bad_shown; int fallback_in_dark;
	T pend_hold, prev_hold; int pend_surf, prev_surf; T pend_acc, prev_acc; T hold_age_max;  /* holds: start, -1 = none */
	bool fast_on, fast_flip;
	int flips_after; bool noflip;
	struct { T t; uint32_t kind; bool off; uint32_t gen; } q[64]; int qn;
	T call_at, timeout_at, next_tick, held_down;
	/* measures */
	T dark_on, dark_on_max, leak, leak_max;
	int debt_dropped;               /* the debt dropped with iOS on while the screen does not show what was kept */
	bool debt_at_end;
} w;

static int visible(void) { return w.frozen ? UNKNOWN : w.buf[w.front]; }

static struct sfs_fast f;
static int kept; static bool boot_done;

static void
fallback(void)
{
	if (w.noflip) w.fallback_in_dark++;    /* started by a frame presented while black, not by 2152's own flips */
	w.stopped = true;
	if (w.front != 0) w.buf[0] = w.s->hw ? BLACK : w.buf[w.front];         /* astSeedBoot */
	if (w.s->fail_restore) w.frozen = true;
	else w.front = 0;
}

static void
ast_flip(int i)
{
	if (w.stopped || w.noflip) return;
	bool fail = false;
	if (w.fast_flip && w.now >= w.s->fail_after) {      /* only 2152's own flips fail: the old paths' failures are not its doing */
		w.flips_after++;
		for (int k = 0; k < 4; k++) if (w.s->fail_flip[k] == w.flips_after) fail = true;
	}
	if (fail) { fallback(); return; }
	w.front = i;
}

static void
blackout(void)
{
	if (!w.stopped) { int back = w.front == 1 ? 2 : 1; w.buf[back] = BLACK; ast_flip(back); return; }
	if (!w.frozen) w.buf[0] = BLACK;
}

/* hold ages count only while iOS has the display on and the pending frame could be shown: a voided frame, or the display
 * being off, keeps holds on the old path too, and that is not 2152's doing */
static bool presentable(void) { return !w.ios_off && w.pend_valid && w.pend_epoch == w.epoch; }

/* a hold's age counts only its presentable time (presentable() above) */
static void
release_hold(T *h)
{
	*h = -1;
}

static void
present(void)
{
	if (!(w.dirty && w.pend_valid && w.pend_epoch == w.epoch)) return;      /* gpuGateOpen */
	int fr = w.pend_frame, kind = w.pend_kind;
	w.dirty = false;
	if (kind == R_ABORT && !w.s->hw) kind = R_NORMAL;       /* without hardware scale the default surface is copied, no abort */
	if (!w.stopped) {
		int back = w.front == 1 ? 2 : 1;
		int *dst = &w.buf[back];
#ifdef NEW
		if (w.noflip) {
#ifdef V4
#ifndef MUT_NO_PREFLIGHT
			if (kind == R_ABORT) { release_hold(&w.pend_hold); release_hold(&w.prev_hold); return; }     /* refused, nothing written */
#endif
#ifdef MUT_STAGE_VALID_EARLY
			w.stage_valid = true;           /* complete as soon as writing starts */
#else
			w.stage_valid = false;          /* incomplete until astFlip */
#endif
			dst = &w.stage;
#else
			dst = &w.buf[kept];             /* v3: the kept slot itself */
#endif
		}
#endif
		if (kind == R_ABORT) { *dst = PARTIAL; release_hold(&w.pend_hold); release_hold(&w.prev_hold); fallback(); return; }
		*dst = kind == R_EPOCH ? INVALID : fr;
		release_hold(&w.pend_hold);     /* 2120: after the copy, before the latch wait */
		release_hold(&w.prev_hold);
		if (kind == R_EPOCH) { w.epoch++; return; }     /* present: copyEpoch != sweet_gpu_epoch(): no astFlip */
#if defined(NEW) && defined(V4)
		if (w.noflip) { w.stage_valid = true; return; } /* astFlip in no-flip mode: sweet_fb_stage_done */
#endif
		ast_flip(back);
	} else {
		if (!w.frozen) w.buf[0] = fr;
		release_hold(&w.pend_hold);
		release_hold(&w.prev_hold);
	}
}

static void
swap(void)
{
	int surf = (w.frame_no + 1) & 1;
	if ((w.pend_hold >= 0 && w.pend_surf == surf) || (w.prev_hold >= 0 && w.prev_surf == surf)) {
		return;                 /* CoreAnimation waits for its surface's hold (hold-wait): no swap this time */
	}
	if (w.s->keep && w.dirty && w.pend_hold >= 0) {          /* sweetfb_keep: the replaced swap is kept, with its hold */
		release_hold(&w.prev_hold);
		w.prev_hold = w.pend_hold; w.prev_surf = w.pend_surf; w.prev_acc = w.pend_acc;
	} else {
		release_hold(&w.pend_hold);
	}
	w.pend_frame = ++w.frame_no; w.pend_epoch = w.epoch; w.pend_valid = true; w.dirty = true;
	w.pend_kind = w.next_kind; w.next_kind = R_NORMAL;
	w.pend_hold = w.now; w.pend_surf = surf; w.pend_acc = 0;
}

static void
request_power(bool off)
{
	if (off == w.ios_off) return;
	w.ios_off = off; w.gen++; w.dirty = true; w.black_done = false;
#ifdef NEW
	if (w.call_at < 0) w.call_at = w.now + w.s->consume;
#endif
}

static void
drain(struct sfs_fast_ev *evs, unsigned *n)
{
	*n = 0;
	int k = 0;
	for (int i = 0; i < w.qn; i++) {
		if (w.q[i].t + w.s->consume <= w.now) { evs[*n].kind = w.q[i].kind; evs[*n].ios_off = w.q[i].off; evs[*n].gen = w.q[i].gen; (*n)++; }
		else w.q[k++] = w.q[i];
	}
	w.qn = k;
}

#ifdef OLD
/* v2's pixel glue: fastRun with fFastPanel false, and the tick returning while dark */
static void
fast_run(void)
{
	struct sfs_fast_ev evs[64]; unsigned n;
	drain(evs, &n);
	bool canBlank = !w.stopped && w.front != 0;
	uint32_t ops = sfs_fast_drain(&f, 1, evs, n, false, w.ios_off, w.gen, false, canBlank, (uint64_t)w.now, WINDOW);
	if (ops & SFS_OP_RESTORE) w.dirty = true;
	w.fast_flip = true;
	if (ops & SFS_OP_BLANK) { kept = w.front; blackout(); }
	if ((ops & SFS_OP_RESTORE) && kept != 0 && !w.ios_off && !w.stopped) ast_flip(kept);
	w.fast_flip = false;
	if (!f.dark) {
		if (kept != 0 && !w.ios_off && visible() != w.buf[kept]) w.debt_dropped++;
		kept = 0;
	}
	if (ops & SFS_OP_BLANK) w.timeout_at = (T)f.until + 1;
}

static void
tick(void)
{
	fast_run();
	if (w.ios_off) { if (!w.black_done) { w.black_done = true; blackout(); } return; }
	if (f.dark) return;
	present();
}
#else
static bool
fast_restore(void)
{
	for (int i = 0; i < 3; i++) {
		int step = sfs_fast_keep_next(w.ios_off, w.stopped, w.frozen, w.front == kept, boot_done);
#ifdef MUT_CLEAR_ON_FAIL
		if (step == SFS_KEEP_BOOT) step = SFS_KEEP_DONE;        /* v2: a flip that did not latch drops the debt */
#endif
		if (step == SFS_KEEP_DONE) {
			if (!w.ios_off && visible() != w.buf[kept]) w.debt_dropped++;
			kept = 0; boot_done = false; w.stage_valid = false; return !w.ios_off;
		}
		if (step == SFS_KEEP_WAIT) return false;
		if (step == SFS_KEEP_FLIP) {
#ifdef V4
			if (w.stage_valid) { w.buf[kept] = w.stage; w.stage_valid = false; }
#endif
			ast_flip(kept);
		} else {
#ifdef V4
			int src = w.stage_valid ? w.stage : w.buf[kept];
#else
			int src = w.buf[kept];
#endif
#ifdef MUT_BOOT_BLACK
			w.buf[0] = w.s->hw ? BLACK : src;       /* the hardware-scale canvas not scaled into the boot buffer */
#else
			w.buf[0] = src;                 /* fastShowInBoot: written even when scanout is unknown */
#endif
			boot_done = true;
		}
	}
	return false;
}

static void
fast_run(void)
{
	struct sfs_fast_ev evs[64]; unsigned n;
	drain(evs, &n);
	bool canBlank = kept == 0 && !w.stopped && w.front != 0;
	uint32_t ops = sfs_fast_drain(&f, 1, evs, n, false, w.ios_off, w.gen, canBlank, (uint64_t)w.now, WINDOW);
	w.fast_flip = true;
#ifndef MUT_NO_BLANK_FAIL
	if (f.dark && w.stopped) f.dark = false;
#endif
	if (ops & SFS_OP_BLANK) {
		kept = w.front; boot_done = false; w.stage_valid = false;
		blackout();
#ifndef MUT_NO_BLANK_FAIL
		if (w.stopped) f.dark = false;
#endif
	}
	if (kept != 0 && !f.dark) fast_restore();
	w.fast_flip = false;
	if (f.dark && (ops & SFS_OP_BLANK)) w.timeout_at = (T)f.until + 1;
	else if (kept != 0 && !f.dark && w.timeout_at < 0) w.timeout_at = w.now + (w.frozen ? 1000 : 50) * MS;
}

static void
tick(void)
{
	fast_run();
	if (w.ios_off) { if (!w.black_done) { w.black_done = true; blackout(); } return; }
#ifdef MUT_EARLY_RETURN
	if (f.dark) return;             /* v2: no frame is taken while dark, so its holds stay */
#endif
	w.noflip = f.dark;
	present();
	w.noflip = false;
}
#endif

static void
kernel_event(uint32_t kind)
{
	if (!kind || !w.fast_on) return;        /* sweetpwrfast=0: the kernel queues nothing */
	w.q[w.qn].t = w.now; w.q[w.qn].kind = kind; w.q[w.qn].off = w.ios_off; w.q[w.qn].gen = w.gen; w.qn++;
	if (w.call_at < 0) w.call_at = w.now + w.s->consume;
}

static void
input(int what)
{
	if (what == E_PRESS) {
		w.held_down = w.now;
		kernel_event(SFS_FAST_PRESS);
		if (w.ios_off && w.s->wake_on_down >= 0) { w.ios_pending = true; w.ios_to = false; w.ios_at = w.now + w.s->wake_on_down; }
	} else if (what == E_RELEASE) {
		kernel_event(w.now - w.held_down < 700 * MS ? SFS_FAST_SHORT : 0);
		if (!w.ios_off && !w.ios_pending && w.s->lock_on_release >= 0) {
			w.ios_pending = true; w.ios_to = true; w.ios_at = w.now + w.s->lock_on_release;
		}
	} else if (what == E_TOUCH) {
		kernel_event(SFS_FAST_OTHER);
	} else if (what == E_GPU_RECOVERY) {
		w.epoch++;
	} else if (what == E_SWAPS_OFF) {
		w.swaps = false;
	} else if (what == E_IOS_OFF) {
		request_power(true);
	} else if (what == E_ABORT_NEXT) {
		w.next_kind = R_ABORT;
	} else if (what == E_EPOCH_NEXT) {
		w.next_kind = R_EPOCH;
	}
}

static void
measure(void)
{
	int v = visible();
	w.dark_on = !w.ios_off && v < 0 ? w.dark_on + STEP : 0;
	if (w.dark_on > w.dark_on_max) w.dark_on_max = w.dark_on;
	w.leak = w.ios_off && v >= 0 ? w.leak + STEP : 0;
	if (!w.ios_off && (v == PARTIAL || v == INVALID)) w.bad_shown += STEP;
	if (w.leak > w.leak_max) w.leak_max = w.leak;
	if (presentable() && w.pend_hold >= 0 && (w.pend_acc += STEP) > w.hold_age_max) w.hold_age_max = w.pend_acc;
	if (presentable() && w.prev_hold >= 0 && (w.prev_acc += STEP) > w.hold_age_max) w.hold_age_max = w.prev_acc;
}

static void
run(const struct scen *s, unsigned seed, bool fast_on)
{
	memset(&w, 0, sizeof(w));
	w.fast_on = fast_on;
	memset(&f, 0, sizeof(f));
	kept = 0; boot_done = false;
	srand(seed);
	w.s = s;
	w.buf[0] = 0; w.buf[1] = 0; w.buf[2] = 0; w.front = 1;        /* frame 0 on screen, flipped once */
	w.swaps = true; w.next_swap = 33 * MS; w.next_tick = TICK; w.call_at = -1; w.timeout_at = -1;
	w.pend_hold = w.prev_hold = -1;
	int ei = 0;
	for (w.now = 0; w.now < s->end; w.now += STEP) {
		while (ei < 12 && s->ev[ei].what != E_END && s->ev[ei].t <= w.now) input(s->ev[ei++].what);
		if (w.ios_pending && w.now >= w.ios_at) { w.ios_pending = false; request_power(w.ios_to); }
		if (w.swaps && !w.ios_off && w.now >= w.next_swap) { swap(); w.next_swap += 33 * MS; }
		if (w.call_at >= 0 && w.now >= w.call_at) { w.call_at = -1; fast_run(); }
		if (w.timeout_at >= 0 && w.now >= w.timeout_at) { w.timeout_at = -1; fast_run(); }
		if (w.now >= w.next_tick) { w.next_tick += TICK; tick(); }
		measure();
	}
	w.debt_at_end = kept != 0;
}

#define EV(...) { __VA_ARGS__, { 0, E_END } }
static const struct scen scens[] = {
	/* still fixed from v1: a GPU recovery voids the pending frame during the dark step and no new swap comes */
	{ "f1-pixel-gpu-recovery", false, false, 1 * MS, -1, -1, { 0 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }), 9000 * MS },
	/* still fixed from v1: a wake press drained after iOS turned on, its release in a later batch */
	{ "f2-wake-press", false, false, 20 * MS, 5 * MS, -1, { 0 }, 0, false,
	  EV({ 500 * MS, E_IOS_OFF }, { 1000 * MS, E_PRESS }, { 1120 * MS, E_RELEASE }), 6000 * MS },
	/* finding 4: the black flip does not latch (hardware scale: the fallback seeds black), then the frame is voided */
	{ "f4-blank-flip-fails-hw", true, false, 1 * MS, -1, -1, { 1 }, 0, false,
	  EV({ 900 * MS, E_GPU_RECOVERY }, { 900 * MS, E_SWAPS_OFF }, { 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 9000 * MS },
	/* finding 4: the flip back does not latch, hardware scale, frame voided during the window */
	{ "f4-restore-flip-fails-hw", true, false, 1 * MS, -1, -1, { 2 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }), 9000 * MS },
	/* finding 4: the same without hardware scale (the fallback seeds the boot buffer from the black front) */
	{ "f4-restore-flip-fails", false, false, 1 * MS, -1, -1, { 2 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }), 9000 * MS },
	/* finding 4: the flip back fails and the fallback cannot restore scanout (frozen): the debt must stay until iOS is off */
	{ "f4-restore-frozen", true, false, 1 * MS, -1, -1, { 2 }, 0, true,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }, { 7000 * MS, E_IOS_OFF }), 9000 * MS },
	/* finding 6: sweetfb_keep, iOS keeps drawing during the window and ignores the press */
	{ "f6-holds-keep", true, true, 1 * MS, -1, -1, { 0 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 5000 * MS },
	/* audit 3 finding 2: while black, a two-layer record whose second layer is the default surface (present writes the first
	 * layer, then astFallback); then no frame comes */
	{ "f2-abort-multilayer", true, false, 1 * MS, -1, -1, { 0 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1500 * MS, E_ABORT_NEXT }, { 1530 * MS, E_SWAPS_OFF }), 6000 * MS },
	/* audit 3 finding 2: while black, a frame refused by the epoch check after it was written; then no frame comes */
	{ "f2-epoch-reject", true, false, 1 * MS, -1, -1, { 0 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1500 * MS, E_EPOCH_NEXT }, { 1530 * MS, E_SWAPS_OFF }), 6000 * MS },
	/* finding 6 without keep */
	{ "f6-holds", true, false, 1 * MS, -1, -1, { 0 }, 0, false,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 5000 * MS },
};

int
main(int argc, char **argv)
{
	for (unsigned i = 0; i < sizeof(scens) / sizeof(scens[0]); i++) {
		run(&scens[i], 1, true);
		printf("%s dark_on_max_ms=%lld leak_max_ms=%lld hold_age_max_ms=%lld debt_dropped=%d debt_at_end=%d bad_shown_ms=%lld fallback_in_dark=%d\n",
		    scens[i].name, w.dark_on_max / MS, w.leak_max / MS, w.hold_age_max / MS, w.debt_dropped, w.debt_at_end, w.bad_shown / MS,
		    w.fallback_in_dark);
	}
#ifdef NEW
	/* random walk: presses, touches, iOS latencies (or none), GPU recoveries, swaps stopping, failing flips, frozen scanout */
	unsigned bad = 0, runs = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
	T worst_dark = 0, worst_leak = 0, worst_hold = 0;
	for (unsigned r = 0; r < runs; r++) {
		srand(5000 + r);
		struct scen s;
		memset(&s, 0, sizeof(s));
		s.name = "fuzz"; s.hw = rand() & 1; s.keep = rand() & 1;
		s.consume = (rand() % 40) * MS;
		s.wake_on_down = rand() % 5 ? (rand() % 400) * MS : -1;
		s.lock_on_release = rand() % 4 ? (rand() % 2500) * MS : -1;
		if (rand() % 3 == 0) { s.fail_after = (rand() % 3000) * MS; s.fail_flip[0] = 1 + rand() % 3; }
		s.fail_restore = rand() % 8 == 0;
		T t = 500 * MS;
		int k = 0;
		while (k < 10) {
			int what = rand() % 10;
			if (what < 5) { s.ev[k++] = (struct sev){ t, E_PRESS }; t += (40 + rand() % 900) * MS; if (k < 10) s.ev[k++] = (struct sev){ t, E_RELEASE }; }
			else if (what < 7) s.ev[k++] = (struct sev){ t, E_TOUCH };
			else if (what < 8) s.ev[k++] = (struct sev){ t, E_GPU_RECOVERY };
			else if (what < 9) s.ev[k++] = (struct sev){ t, rand() % 3 == 0 ? E_SWAPS_OFF : (rand() & 1 ? E_ABORT_NEXT : E_EPOCH_NEXT) };
			t += (rand() % 1500) * MS;
		}
		s.ev[k < 12 ? k : 11].what = E_END;
		s.end = t + 4000 * MS;
		/*
		 * Differential: the same run with sweetpwrfast=0 is what the old paths do anyway (a wake with only voided frames stays
		 * dark, for one). 2152 may add one window of darkness, nothing else: no longer leak, no longer hold, no dropped debt.
		 */
		run(&s, 5000 + r, false);
		T base_dark = w.dark_on_max, base_leak = w.leak_max, base_hold = w.hold_age_max;
		run(&s, 5000 + r, true);
		bool frozen_run = w.frozen;
		/* with frozen scanout no frame of any kind can be shown: only the debt rule applies there */
		T dark_cap = base_dark > WINDOW + 200 * MS ? base_dark : WINDOW + 200 * MS;
		bool v = (!frozen_run && w.dark_on_max > dark_cap) || w.leak_max > (base_leak > 40 * MS ? base_leak : 40 * MS) ||
		    w.debt_dropped != 0 || w.hold_age_max > (base_hold > 60 * MS ? base_hold : 60 * MS);
#ifdef V4
		v = v || w.bad_shown != 0 || w.fallback_in_dark != 0;  /* never an incomplete image, never a fallback of its own */
#endif
		if (!frozen_run && w.dark_on_max > worst_dark) worst_dark = w.dark_on_max;
		if (w.leak_max > worst_leak) worst_leak = w.leak_max;
		if (w.hold_age_max > worst_hold) worst_hold = w.hold_age_max;
		if (v && bad++ < 5) {
			printf("fuzz %u: dark_on %lld ms leak %lld ms debt_dropped %d hold %lld ms bad %lld ms fallback_in_dark %d (hw %d keep %d frozen %d)\n",
			    r, w.dark_on_max / MS, w.leak_max / MS, w.debt_dropped, w.hold_age_max / MS, w.bad_shown / MS, w.fallback_in_dark,
			    s.hw, s.keep, frozen_run);
		}
	}
	printf("fuzz runs=%u violations=%u worst_dark_on_ms=%lld worst_leak_ms=%lld worst_hold_ms=%lld\n", runs, bad,
	    worst_dark / MS, worst_leak / MS, worst_hold / MS);
#endif
	return 0;
}
