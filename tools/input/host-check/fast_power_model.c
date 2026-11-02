/*
 * 2152 regression model: each audit finding (analysis/input-ff-audit.md, findings 1, 2, 6, 7) as a timed scenario, run on
 * the v1 logic (-DOLD: v1 sfs_fast_step, v1 glue, the old panel thread) and on v2 (-DNEW: sfs_fast_drain, v2 glue, the
 * panel thread walking panel_plan with retries). The pure parts are the real code: OLD includes v1's SweetFbScale.h, NEW
 * includes v2's and the panel_plan cut from v2's sweet_dsi_asteroids.c (fast_power_model.py does the cutting). The glue,
 * iOS, the GPU gate, frames and the DCS link are modelled from the kernel sources, at 0.25 ms steps.
 *
 * Each scenario prints what it measures. fast_power_model.py asserts the old build fails it and the new one passes.
 * NEW also runs a random walk with the invariants: never dark while iOS has the display on for longer than the window plus
 * 600 ms, never the pre-lock image lit while iOS has it off for longer than 40 ms.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SweetFbScale.h"
#ifdef NEW
#include "panel_plan.inc"       /* PANEL_* and panel_plan(), cut from the patched driver */
#else
#define PANEL_ON 0
#define PANEL_DARK 1
#define PANEL_ASLEEP 2
#endif

typedef long long T;            /* microseconds */
#define MS 1000LL
#define STEP 250LL
#define TICK 16667LL
#define WINDOW (2000 * MS)
#define BLACK 0
#define IMAGE 1

enum { E_PRESS, E_RELEASE, E_TOUCH, E_VOL, E_GPU_RECOVERY, E_SWAPS_OFF, E_END };
struct sev { T t; int what; };
struct scen {
	const char *name;
	int mode, panel;                /* sweetpwrfast, sweetpanel */
	bool ios_off0;
	T consume;                      /* thread call + work loop gate latency */
	T wake_on_down, lock_on_down, lock_on_release;  /* iOS's reaction delays, -1 = none */
	int fail_cmd; int fail_count; T fail_after;     /* DCS: this command fails this many times after this time */
	double fail_p;                  /* random DCS failures */
	struct sev ev[16];
	T end;
};

/* ---- world ---- */
static struct {
	T now;
	bool ios_off; uint32_t gen;
	T ios_at; bool ios_to; bool ios_pending;
	uint32_t epoch, pend_epoch; bool pend_valid, swaps; T next_swap;
	bool dirty;
	int front;
	bool panel, panel_off, black_done;
	int phys;
	/* fast path */
	uint32_t mode;
	unsigned flips_asleep;
	/* event delivery */
	struct { T t; uint32_t kind; bool off; uint32_t gen; } q[64]; int qn;
	T next_tick, timeout_at, call_at;
	/* panel thread */
	int target; uint8_t last; T last_t, cmd_done, wake_at; int cmd_next; uint8_t cmd; bool cmd_fail; unsigned fails;
	bool old_req_pending, old_req_on, old_rec_on, old_req_on_seq; int old_seq_i, old_seq_n; uint8_t old_seq[4]; T old_seq_delay[4];
	int fail_left;
	const struct scen *s;
	/* measures */
	T dark_on, dark_on_max, leak, leak_max, lit_off, lit_off_max, awake_off, awake_off_max, awake_off_after_touch; T touch_t;
	T held_down_t;
} w;

#ifdef OLD
static struct sfs_fast of;
static uint32_t old_bits;
static T old_bits_t;
#else
static struct sfs_fast nf;
static bool fast_panel; static int kept;
static T wake_abs;
#endif

static double rnd(void) { return rand() / (double)RAND_MAX; }

static bool
dcs_fails(uint8_t cmd)
{
	if (w.s->fail_p > 0 && rnd() < w.s->fail_p) return true;
	if (cmd == w.s->fail_cmd && w.now >= w.s->fail_after && w.fail_left > 0) { w.fail_left--; return true; }
	return false;
}

static void
phys_apply(uint8_t cmd)
{
	if (cmd == 0x28 && w.phys == PANEL_ON) w.phys = PANEL_DARK;
	else if (cmd == 0x10) w.phys = PANEL_ASLEEP;
	else if (cmd == 0x11 && w.phys == PANEL_ASLEEP) w.phys = PANEL_DARK;
	else if (cmd == 0x29 && w.phys == PANEL_DARK) w.phys = PANEL_ON;
}

/* ---- panel thread ---- */
static void
post_target(int t)
{
	if (!w.panel) return;
#ifdef NEW
	w.target = t;
	if (w.cmd == 0 && w.wake_at > w.now) w.wake_at = w.now;
#else
	w.old_req_pending = true;
	w.old_req_on = t == PANEL_ON;
#endif
}

static void
panel_thread(void)
{
	if (!w.panel) return;
	if (w.cmd != 0) {                       /* a command in flight */
		if (w.now < w.cmd_done) return;
		uint8_t c = w.cmd;
		w.cmd = 0;
		if (!w.cmd_fail) phys_apply(c);
#ifdef NEW
		if (!w.cmd_fail) { w.last = c; w.last_t = w.now; w.fails = 0; w.wake_at = w.now; }
		else { T back = (20LL << (w.fails < 6 ? w.fails : 6)) * MS; w.fails++; w.wake_at = w.now + back; }
#else
		w.wake_at = w.now + w.old_seq_delay[w.old_seq_i++];
		if (w.old_seq_i == w.old_seq_n) { w.old_rec_on = w.old_req_on_seq; }
#endif
		return;
	}
	if (w.now < w.wake_at) return;
#ifdef NEW
	int next; unsigned gap;
	uint8_t c = panel_plan(w.phys, w.target, w.last, &next, &gap);
	if (c == 0) { w.wake_at = w.now + 100 * MS; return; }
	if (w.now < w.last_t + gap * MS) { w.wake_at = w.last_t + gap * MS; return; }
	w.cmd = c; w.cmd_fail = dcs_fails(c); w.cmd_done = w.now + (w.cmd_fail ? 50 : 8) * MS;
#else
	if (w.old_seq_i < w.old_seq_n) {        /* the sequence goes on whatever came back */
		uint8_t c = w.old_seq[w.old_seq_i];
		w.cmd = c; w.cmd_fail = dcs_fails(c); w.cmd_done = w.now + (w.cmd_fail ? 50 : 8) * MS;
		return;
	}
	if (w.old_req_pending) {
		w.old_req_pending = false;
		bool on = w.old_req_on;
		if (on != w.old_rec_on) {
			w.old_req_on_seq = on;
			w.old_seq_i = 0; w.old_seq_n = 2;
			w.old_seq[0] = on ? 0x11 : 0x28; w.old_seq_delay[0] = (on ? 80 : 120) * MS;
			w.old_seq[1] = on ? 0x29 : 0x10; w.old_seq_delay[1] = (on ? 10 : 100) * MS;
			return;
		}
	}
	w.wake_at = w.now + STEP;
#endif
}

/* ---- display ---- */
static void
flip(int what)
{
	if (w.panel && w.phys == PANEL_ASLEEP) w.flips_asleep++;
	w.front = what;
}

static void
present(void)
{
	if (w.dirty && w.pend_valid && w.pend_epoch == w.epoch) {       /* gpuGateOpen: done and of this epoch */
		flip(IMAGE);
		w.dirty = false;
	}
}

static void
request_power(bool off)
{
	if (off == w.ios_off) return;
	w.ios_off = off;
	w.gen++;
	w.dirty = true;
	w.black_done = false;
#ifdef NEW
	if (w.mode && (w.call_at < 0 || w.call_at > w.now + w.s->consume)) w.call_at = w.now + w.s->consume;     /* thread_call_enter */
#endif
}

#ifdef OLD
static void
old_apply(uint32_t act)
{
	if (act & SFS_DO_DARK) { flip(BLACK); if (w.panel && !w.panel_off) { w.panel_off = true; post_target(PANEL_ASLEEP); } }
	if (act & SFS_DO_WAKE) { w.panel_off = false; post_target(PANEL_ON); }
}

static void
old_tick(void)
{
	uint32_t act = w.mode ? sfs_fast_step(&of, w.mode, 0, w.ios_off, w.panel, w.panel_off, w.now, WINDOW) : 0;
	if (act & SFS_DO_SHOW) w.dirty = true;
	if (w.ios_off) {
		if (w.panel) {
			if (!w.panel_off && !of.awake) { flip(BLACK); w.panel_off = true; post_target(PANEL_ASLEEP); }
			return;
		}
		if (!w.black_done) { w.black_done = true; flip(BLACK); }
		return;
	}
	if (of.dark) return;
	present();
	if (w.panel && w.panel_off && !w.dirty) { w.panel_off = false; post_target(PANEL_ON); }
}
#else
static void
fast_run(void)
{
	struct sfs_fast_ev evs[64];
	unsigned n = 0;
	for (int i = 0; i < w.qn; i++) if (w.q[i].t + w.s->consume <= w.now) { evs[n].kind = w.q[i].kind; evs[n].ios_off = w.q[i].off; evs[n].gen = w.q[i].gen; n++; }
	int k = 0;
	for (int i = 0; i < w.qn; i++) if (!(w.q[i].t + w.s->consume <= w.now)) w.q[k++] = w.q[i];
	w.qn = k;
	bool canBlank = w.panel ? w.phys == PANEL_ON : w.front == IMAGE;
	bool canWake = w.panel && w.panel_off && w.phys == PANEL_ASLEEP;
	bool active0 = nf.dark || nf.awake;
	uint32_t ops = sfs_fast_drain(&nf, w.mode, evs, n, false, w.ios_off, w.gen, canWake, canBlank, (uint64_t)w.now, WINDOW);
	if (ops & SFS_OP_BLANK) fast_panel = w.panel;
	if (fast_panel && (ops & (SFS_OP_BLANK | SFS_OP_RESTORE))) post_target((ops & SFS_OP_RESTORE) ? PANEL_ON : PANEL_DARK);
	if (ops & (SFS_OP_WAKE | SFS_OP_SLEEP)) post_target((ops & SFS_OP_WAKE) ? PANEL_DARK : PANEL_ASLEEP);
	if (ops & SFS_OP_RESTORE) w.dirty = true;
	if ((ops & SFS_OP_BLANK) && !fast_panel) { kept = w.front; flip(BLACK); }
	if ((ops & SFS_OP_RESTORE) && !fast_panel && kept == IMAGE && !w.ios_off) flip(IMAGE);
	if (!nf.dark) kept = BLACK;
	if (ops & (SFS_OP_BLANK | SFS_OP_WAKE)) w.timeout_at = (T)nf.until + 1;
	else if (active0 && !(nf.dark || nf.awake)) w.timeout_at = -1;
}

static void
new_tick(void)
{
	if (w.mode) fast_run();
	if (w.ios_off) {
		if (w.panel) {
			if (!w.panel_off) { flip(BLACK); w.panel_off = true; post_target(PANEL_ASLEEP); }
			else if (!nf.awake && w.phys != PANEL_ASLEEP) post_target(PANEL_ASLEEP);
			wake_abs = -1;
			return;
		}
		if (!w.black_done) { w.black_done = true; flip(BLACK); }
		return;
	}
	if (nf.dark && !fast_panel) return;
	if (w.panel && w.panel_off) {
		if (wake_abs < 0) { wake_abs = w.now; post_target(PANEL_DARK); }
		if (w.phys == PANEL_ASLEEP && w.now - wake_abs < 500 * MS) return;
	}
	present();
	if (w.panel && w.panel_off && !w.dirty) { w.panel_off = false; wake_abs = -1; post_target(PANEL_ON); }
}
#endif

/* ---- input ---- */
static void
kernel_event(uint32_t kind)
{
#ifdef OLD
	if (!w.mode || !kind) return;
	if (!old_bits) old_bits_t = w.now;
	old_bits |= kind;
#else
	if (!w.mode || !kind) return;
	w.q[w.qn].t = w.now; w.q[w.qn].kind = kind; w.q[w.qn].off = w.ios_off; w.q[w.qn].gen = w.gen; w.qn++;
	if (w.call_at < 0 || w.call_at > w.now + w.s->consume) w.call_at = w.now + w.s->consume;
#endif
}

static void
schedule_ios(bool off, T delay)
{
	if (delay < 0) return;
	w.ios_pending = true; w.ios_to = off; w.ios_at = w.now + delay;
}

static void
input(int what)
{
	if (what == E_PRESS) {
		w.held_down_t = w.now;
		kernel_event(SFS_FAST_PRESS);           /* v2 order: before HID. v1 consumed later anyway */
		if (w.ios_off) schedule_ios(false, w.s->wake_on_down);
		else schedule_ios(true, w.s->lock_on_down);
	} else if (what == E_RELEASE) {
		kernel_event(w.now - w.held_down_t < 700 * MS ? SFS_FAST_SHORT : 0);
		if (!w.ios_off && !w.ios_pending) schedule_ios(true, w.s->lock_on_release);
	} else if (what == E_TOUCH || what == E_VOL) {
		kernel_event(SFS_FAST_OTHER);
		w.touch_t = w.now;
	} else if (what == E_GPU_RECOVERY) {
		w.epoch++;
	} else if (what == E_SWAPS_OFF) {
		w.swaps = false;
	}
}

static void
measure(void)
{
	bool dark = w.phys != PANEL_ON || w.front == BLACK;
	w.dark_on = !w.ios_off && dark ? w.dark_on + STEP : 0;
	if (w.dark_on > w.dark_on_max) w.dark_on_max = w.dark_on;
	w.leak = w.ios_off && w.phys == PANEL_ON && w.front == IMAGE ? w.leak + STEP : 0;
	if (w.leak > w.leak_max) w.leak_max = w.leak;
	w.lit_off = w.ios_off && w.phys == PANEL_ON ? w.lit_off + STEP : 0;
	if (w.lit_off > w.lit_off_max) w.lit_off_max = w.lit_off;
	w.awake_off = w.panel && w.ios_off && w.phys != PANEL_ASLEEP ? w.awake_off + STEP : 0;
	if (w.awake_off > w.awake_off_max) w.awake_off_max = w.awake_off;
	if (w.touch_t > 0 && w.ios_off && w.phys != PANEL_ASLEEP) w.awake_off_after_touch += STEP;
}

static void
run(const struct scen *s, unsigned seed)
{
	memset(&w, 0, sizeof(w));
	srand(seed);
	w.s = s; w.mode = s->mode; w.panel = s->panel != 0; w.fail_left = s->fail_count;
	w.ios_off = s->ios_off0; w.panel_off = s->ios_off0 && w.panel;
	w.phys = w.panel && s->ios_off0 ? PANEL_ASLEEP : PANEL_ON;
	w.front = s->ios_off0 ? BLACK : IMAGE; w.black_done = s->ios_off0;
	w.target = w.phys; w.old_rec_on = w.phys == PANEL_ON;
	w.pend_valid = true; w.swaps = true; w.next_swap = 33 * MS; w.next_tick = TICK; w.timeout_at = -1; w.call_at = -1;
	w.touch_t = 0; w.dark_on_max = 0;
#ifdef OLD
	memset(&of, 0, sizeof(of)); old_bits = 0;
#else
	memset(&nf, 0, sizeof(nf)); fast_panel = false; kept = BLACK; wake_abs = -1;
#endif
	int ei = 0;
	for (w.now = 0; w.now < s->end; w.now += STEP) {
		while (ei < 16 && s->ev[ei].what != E_END && s->ev[ei].t <= w.now) input(s->ev[ei++].what);
		if (w.ios_pending && w.now >= w.ios_at) { w.ios_pending = false; request_power(w.ios_to); }
		if (w.swaps && !w.ios_off && w.now >= w.next_swap) { w.pend_valid = true; w.pend_epoch = w.epoch; w.dirty = true; w.next_swap += 33 * MS; }
#ifdef OLD
		if (old_bits && w.now >= old_bits_t + s->consume) {   /* fastAction: OR'd bits, iOS's state now */
			uint32_t act = sfs_fast_step(&of, w.mode, old_bits, w.ios_off, w.panel, w.panel_off, w.now, WINDOW);
			old_bits = 0;
			if (act & SFS_DO_SHOW) w.dirty = true;
			old_apply(act);
		}
		if (w.now >= w.next_tick) { w.next_tick += TICK; old_tick(); }
#else
		if (w.call_at >= 0 && w.now >= w.call_at) { w.call_at = -1; fast_run(); }
		if (w.timeout_at >= 0 && w.now >= w.timeout_at) { w.timeout_at = -1; fast_run(); }
		if (w.now >= w.next_tick) { w.next_tick += TICK; new_tick(); }
#endif
		panel_thread();
		measure();
	}
}

#define EV(...) { __VA_ARGS__, { 0, E_END } }
static const struct scen scens[] = {
	/* audit 1: panel dark, a GPU recovery voids the pending frame, no new swap, iOS keeps the display on */
	{ "f1-panel-gpu-recovery", 1, 1, false, 1 * MS, -1, -1, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }), 9000 * MS },
	/* audit 1, pixels: the same with sweetpanel=0 */
	{ "f1-pixel-gpu-recovery", 1, 0, false, 1 * MS, -1, -1, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1200 * MS, E_GPU_RECOVERY }, { 1200 * MS, E_SWAPS_OFF }), 9000 * MS },
	/* audit 1, DCS: the command that undoes the dark step times out once, frames keep coming */
	{ "f1-dcs-restore-fails", 1, 1, false, 1 * MS, -1, -1, -1, 0x11, 1, 2000 * MS, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 9000 * MS },
	{ "f1-dcs-restore-fails-29", 1, 1, false, 1 * MS, -1, -1, -1, 0x29, 1, 2000 * MS, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 9000 * MS },
	/* audit 1, DCS the other way: an early wake is undone and that command times out once (lit, or awake, while off) */
	{ "f1-dcs-unwake-fails-10", 2, 1, true, 1 * MS, -1, -1, -1, 0x10, 1, 1500 * MS, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 9000 * MS },
	/* audit 2: a wake press, iOS turns on 5 ms after the HID event, the PRESS is consumed 20 ms later, release in a later batch */
	{ "f2-wake-press-darkened", 1, 0, true, 20 * MS, 5 * MS, -1, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1120 * MS, E_RELEASE }), 6000 * MS },
	/* audit 2, the other way: a lock press iOS acts on at once is consumed late and taken for a wake */
	{ "f2-lock-press-woken", 3, 1, false, 20 * MS, -1, 5 * MS, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1120 * MS, E_RELEASE }), 6000 * MS },
	/* audit 6: early wake, then a touch while iOS stays off: the panel must go back to sleep at the touch */
	{ "f6-touch-cancels-wake", 2, 1, true, 1 * MS, -1, -1, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }, { 1300 * MS, E_TOUCH }), 6000 * MS },
	/* audit 7: iOS turns the display on while the panel sleeps: no flip may wait on a sleeping panel */
	{ "f7-flip-while-asleep", 0, 1, true, 1 * MS, 30 * MS, -1, -1, 0, 0, 0, 0,
	  EV({ 1000 * MS, E_PRESS }, { 1100 * MS, E_RELEASE }), 4000 * MS },
};

int
main(int argc, char **argv)
{
	for (unsigned i = 0; i < sizeof(scens) / sizeof(scens[0]); i++) {
		run(&scens[i], 1);
		printf("%s dark_on_max_ms=%lld leak_max_ms=%lld lit_off_max_ms=%lld awake_off_max_ms=%lld awake_after_touch_ms=%lld flips_asleep=%u\n",
		    scens[i].name, w.dark_on_max / MS, w.leak_max / MS, w.lit_off_max / MS, w.awake_off_max / MS, w.awake_off_after_touch / MS,
		    w.flips_asleep);
	}
#ifdef NEW
	/* random walk over presses, touches, iOS latencies (including ignoring a press), GPU recoveries and DCS failures */
	unsigned bad = 0, runs = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
	T worst_dark = 0, worst_leak = 0;
	for (unsigned r = 0; r < runs; r++) {
		srand(1000 + r);
		struct scen s;
		memset(&s, 0, sizeof(s));
		s.name = "fuzz"; s.mode = 1 + rand() % 3; s.panel = rand() % 2; s.ios_off0 = rand() % 2;
		s.consume = (rand() % 40) * MS;
		s.wake_on_down = rand() % 5 ? (rand() % 400) * MS : -1;
		s.lock_on_down = rand() % 8 == 0 ? (rand() % 50) * MS : -1;
		s.lock_on_release = rand() % 5 ? (rand() % 2500) * MS : -1;
		s.fail_p = 0.05; s.fail_cmd = -1;
		T t = 500 * MS;
		int k = 0;
		while (k < 14) {
			int what = rand() % 10;
			if (what < 4) { s.ev[k++] = (struct sev){ t, E_PRESS }; t += (40 + rand() % 900) * MS; if (k < 14) s.ev[k++] = (struct sev){ t, E_RELEASE }; }
			else if (what < 6) s.ev[k++] = (struct sev){ t, E_TOUCH };
			else if (what < 7) s.ev[k++] = (struct sev){ t, E_VOL };
			else if (what < 8) s.ev[k++] = (struct sev){ t, E_GPU_RECOVERY };
			t += (rand() % 1500) * MS;
		}
		s.ev[15].what = E_END;
		if (k < 15) s.ev[k].what = E_END;
		s.end = t + 4000 * MS;
		run(&s, 1000 + r);
		/* a press iOS never answers, a void pending frame with no new swap: the screen still comes back */
		if (w.dark_on_max > worst_dark) worst_dark = w.dark_on_max;
		if (w.leak_max > worst_leak) worst_leak = w.leak_max;
		if (w.dark_on_max > WINDOW + 600 * MS || w.leak_max > 40 * MS) {
			if (bad++ < 5) printf("fuzz %u: dark_on_max %lld ms leak %lld ms (mode %d panel %d)\n", r, w.dark_on_max / MS, w.leak_max / MS, s.mode, s.panel);
		}
	}
	printf("fuzz runs=%u violations=%u worst_dark_on_ms=%lld worst_leak_ms=%lld\n", runs, bad, worst_dark / MS, worst_leak / MS);
#endif
	return 0;
}
