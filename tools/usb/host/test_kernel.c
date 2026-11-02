/*
 * The audit's findings as host tests, run against the real asteroids gadget code (host/kernel-harness.py cuts it out
 * of a stage's sweet_usb.c) and a mock DWC3. Each test names its finding. The mock is a model written from the DWC3
 * register descriptions used in the code and from Linux's core.h; it proves the control logic, not the hardware.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_usb_gadget.h"

typedef uintptr_t vm_offset_t, vm_size_t;
typedef uint64_t addr64_t;
typedef uint32_t ppnum_t;
typedef int kern_return_t, errno_t, wait_result_t;
typedef void *thread_t, *ifnet_t, *mbuf_t, *socket_t;
typedef struct { int x; } lck_mtx_t, lck_grp_t;
typedef uint32_t UInt32;
#define __unused __attribute__((unused))
#define KERN_SUCCESS 0
#define PAGE_SIZE ((vm_size_t)16384)
#define PAGE_MASK (PAGE_SIZE - 1)
#define round_page(x) (((x) + PAGE_MASK) & ~PAGE_MASK)
#define ptoa_64(pn) ((uint64_t)(pn) << 14)
#define KMA_DATA 1
#define KMA_ZERO 2
#define KMA_PERMANENT 4
#define VM_KERN_MEMORY_IOKIT 0
#define NSEC_PER_SEC 1000000000ull
#define NSEC_PER_MSEC 1000000ull
#define LCK_GRP_ATTR_NULL NULL
#define LCK_ATTR_NULL NULL
static void *kernel_map, *kernel_pmap;
static unsigned long gPhysBase = 0x880000000ul, gPhysSize = 0x200000000ul;

static int mock_printf(const char *fmt, ...);
#define printf mock_printf
static void sweet_log_sync(void);
static void sweet_dma_sync_va(const void *va, size_t len) { (void)va; (void)len; }
static void delay(int us);
static void ud_sleep_ms(unsigned ms);
static uint64_t mach_absolute_time(void);
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *ns) { *ns = t; }
static void panic(const char *fmt, ...);
static vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t size);
static kern_return_t kmem_alloc_contig(void *map, vm_offset_t *addr, vm_size_t size, vm_offset_t mask, ppnum_t max,
    ppnum_t pnmask, int flags, int tag);
static ppnum_t pmap_find_phys(void *pmap, addr64_t va);
static int PE_parse_boot_argn(const char *name, void *out, int size) { (void)name; (void)out; (void)size; return 0; }
static void lck_mtx_lock(lck_mtx_t *l) { (void)l; }
static void lck_mtx_unlock(lck_mtx_t *l) { (void)l; }
static lck_grp_t *lck_grp_alloc_init(const char *n, void *a) { static lck_grp_t g; (void)n; (void)a; return &g; }
static lck_mtx_t *lck_mtx_alloc_init(lck_grp_t *g, void *a) { static lck_mtx_t l; (void)g; (void)a; return &l; }
static kern_return_t kernel_thread_start(void (*fn)(void *, wait_result_t), void *arg, thread_t *t);
static void thread_deallocate(thread_t t) { (void)t; }
static bool sweet_reboot_handsfree_ready(void);
static uint32_t dwc_rd(uint32_t off);
static void dwc_wr(uint32_t off, uint32_t v);
static uint32_t ua_rd(vm_offset_t base, uint32_t off);
static void ua_wr(vm_offset_t addr, uint32_t v);
static void ud_link_event(bool up) { (void)up; }
static void ud_rx_done(void);
static void ud_tx_kick_locked(void) {}
static void ud_if_create(void) {}
static void ud_net_start(void) {}
static void ua_if_inet6(void) {}
static void ud_log_thread(void *a, wait_result_t w) { (void)a; (void)w; }

#include GEN

/* ---- the mock ---- */
#define DWC         0x0a600000u
#define QS          0x0a6f8800u
#define SMMU        0x15000000u
#define PHY         0x088e3000u
#define AREA_PA     0x8c0000000ull
#define ABL_EVT     0xd017b000u
#define R(off)      m.dwc[(off) / 4]

static struct {
	uint32_t dwc[0xd000 / 4], qs[0x100 / 4];
	bool ready, halt_stuck, run_stuck, gevnt_ignore, allocfail, run_seen;
	int cmdioc_mode;                /* 0 none, 1 right, 2 wrong form, 3 at offset 8 */
	int fail_cmd, fail_ep, fail_nth, fail_kind, fail_count;   /* kind 1 status, 2 timeout */
	bool active[16], end_left[16];
	int ending[16];
	uint64_t trb_pa[16];
	int starts[16];
	uint32_t producer;
	uint8_t *area;
	size_t area_size;
	int violations, unmarked;       /* DMA and ownership model violations; unannounced first accesses (F8) */
	char viol[8][200], first_unmarked[200];
	int mmio, writes, qs_writes;
	char log[3000][240];
	int nlog, synced;
	uint64_t now;
	int panics, threads;
	long budget;
	jmp_buf jmp;
	uint32_t seen_rd[64], seen_wr[64];
	int nrd, nwr;
} m;

static void
violation(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (m.violations < 8) {
		vsnprintf(m.viol[m.violations], sizeof(m.viol[0]), fmt, ap);
	}
	va_end(ap);
	m.violations++;
}

static int
mock_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (m.nlog < 3000) {
		vsnprintf(m.log[m.nlog++], sizeof(m.log[0]), fmt, ap);
	}
	va_end(ap);
	if (getenv("UGK_VERBOSE")) {
		fputs(m.log[m.nlog - 1], stdout);
	}
	return 0;
}

static void sweet_log_sync(void) { m.synced = m.nlog; }
static uint64_t mach_absolute_time(void) { return m.now; }
static bool sweet_reboot_handsfree_ready(void) { return m.ready; }
static vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t size) { (void)size; return pa; }

static void
panic(const char *fmt, ...)
{
	(void)fmt;
	m.panics++;
	longjmp(m.jmp, 2);
}

static kern_return_t
kmem_alloc_contig(void *map, vm_offset_t *addr, vm_size_t size, vm_offset_t mask, ppnum_t max, ppnum_t pnmask,
    int flags, int tag)
{
	(void)map; (void)mask; (void)max; (void)pnmask; (void)flags; (void)tag;
	if (m.allocfail) {
		return 3;
	}
	m.area = aligned_alloc(PAGE_SIZE, size);
	memset(m.area, 0, size);
	m.area_size = size;
	*addr = (vm_offset_t)m.area;
	return KERN_SUCCESS;
}

static ppnum_t
pmap_find_phys(void *pmap, addr64_t va)
{
	(void)pmap;
	return (ppnum_t)((AREA_PA + (va - (addr64_t)(uintptr_t)m.area)) >> 14);
}

static uint32_t *
host(uint64_t pa, uint32_t len)
{
	if (m.area == NULL || pa < AREA_PA || pa + len > AREA_PA + m.area_size) {
		return NULL;
	}
	return (uint32_t *)(m.area + (pa - AREA_PA));
}

static kern_return_t
kernel_thread_start(void (*fn)(void *, wait_result_t), void *arg, thread_t *t)
{
	(void)fn; (void)arg; *t = NULL;
	m.threads++;
	return KERN_SUCCESS;
}

static void
tick(void)
{
	for (int ep = 0; ep < 16; ep++) {
		if (m.ending[ep] > 0) {
			m.ending[ep]--;
		}
	}
}

static void delay(int us) { m.now += (uint64_t)us * 1000; tick(); }

static void
ud_sleep_ms(unsigned ms)
{
	m.now += ms * NSEC_PER_MSEC;
	tick();
	if (--m.budget <= 0) {
		longjmp(m.jmp, 1);
	}
}

static void
emit(uint32_t ev)
{
	uint64_t adr = ((uint64_t)R(0xc404) << 32) | R(0xc400);
	uint32_t *p = host(adr + m.producer, 4);
	if (p == NULL) {
		violation("event 0x%08x written at 0x%llx, outside our area%s", ev, adr + m.producer,
		    adr == ABL_EVT ? " (ABL's buffer)" : "");
	} else {
		*p = ev;
	}
	m.producer = (m.producer + 4) % 4096;
	R(0xc40c) += 4;
}

/* DWC3 registers no earlier boot read (the level 1 probe's list) need an announced, synced marker first. */
static bool
proven_read(uint32_t pa)
{
	static const uint32_t probe[] = { 0xc120, 0xc110, 0xc118, 0xc140, 0xc144, 0xc14c, 0xc200, 0xc2c0, 0xc700, 0xc704,
		                          0xc708, 0xc70c, 0xc720, 0xc400, 0xc404, 0xc408, 0xc40c };
	if ((pa >= SMMU && pa < SMMU + 0x100000) || (pa >= PHY && pa < PHY + 0x154)) {
		return true;
	}
	for (unsigned i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
		if (pa == DWC + probe[i]) {
			return true;
		}
	}
	return false;
}

static void
first_access(uint32_t pa, bool write)
{
	uint32_t *seen = write ? m.seen_wr : m.seen_rd;
	int *n = write ? &m.nwr : &m.nrd;
	for (int i = 0; i < *n; i++) {
		if (seen[i] == pa) {
			return;
		}
	}
	if (*n < 64) {
		seen[(*n)++] = pa;
	}
	if (!write && proven_read(pa)) {
		return;
	}
	for (int i = 0; i < m.synced; i++) {
		unsigned a, l;
		if (sscanf(m.log[i], "ASTEROIDS USB MMIO: first access to 0x%x+0x%x", &a, &l) == 2 && pa >= a && pa < a + l) {
			return;
		}
	}
	if (m.unmarked++ == 0) {
		snprintf(m.first_unmarked, sizeof(m.first_unmarked), "first %s of 0x%08x without a synced marker",
		    write ? "write" : "read", pa);
	}
}

static uint32_t
smmu_rd(uint32_t off)
{
	switch (off) {
	case 0x20: return 0x4c017e72;
	case 0x24: return 0x60000048;
	case 0x28: return 0x00005111;
	case 0x48: return 0;
	case 0x800: return 0x80000060;
	case 0x804: return 0x80000540;
	case 0x808: return 0x80021c00;
	case 0x80c: return 0x80000040;
	case 0x810: return 0x80000480;
	case 0xc0c: return 3;
	case (128 + 3) * 4096: return 0xe0;
	default: return 0;
	}
}

static uint32_t
mmio_rd(uint32_t pa)
{
	m.mmio++;
	first_access(pa, false);
	if (pa >= DWC && pa < DWC + 0xd000) {
		return m.dwc[(pa - DWC) / 4];
	}
	if (pa >= QS && pa < QS + 0x100) {
		return m.qs[(pa - QS) / 4];
	}
	if (pa >= SMMU && pa < SMMU + 0x100000) {
		return smmu_rd(pa - SMMU);
	}
	if (pa >= PHY && pa < PHY + 0x1000) {
		return 0;
	}
	violation("read of unknown MMIO 0x%08x", pa);
	return 0;
}

static void
command(unsigned ep, uint32_t v)
{
	uint32_t cmd = v & 0xf, p0 = R(0xc808 + 16 * ep), p1 = R(0xc804 + 16 * ep);
	bool ioc = (v >> 8) & 1;

	if (cmd == (uint32_t)m.fail_cmd && (m.fail_ep < 0 || (int)ep == m.fail_ep) && ++m.fail_count == m.fail_nth) {
		R(0xc80c + 16 * ep) = m.fail_kind == 2 ? v : (cmd | (1u << 12));
		if (cmd == 8) {
			m.end_left[ep] = true;  /* a failed end leaves the transfer running */
		}
		return;
	}
	R(0xc80c + 16 * ep) = cmd;
	switch (cmd) {
	case 6: {                       /* STARTTRANSFER */
		uint64_t pa = ((uint64_t)p0 << 32) | p1;
		uint32_t *t = host(pa, 16);
		m.starts[ep]++;
		if (t == NULL) {
			violation("ep%u TRB at 0x%llx outside our area", ep, pa);
			break;
		}
		if (!(t[3] & 1)) {
			violation("ep%u TRB without HWO", ep);
		}
		if (m.active[ep] || m.ending[ep] > 0 || m.end_left[ep]) {
			violation("ep%u STARTTRANSFER while its transfer %s", ep, m.active[ep] ? "is active" : "is still ending");
		}
		if (host(((uint64_t)t[1] << 32) | t[0], t[2] & 0xffffff) == NULL) {
			violation("ep%u buffer outside our area", ep);
		}
		m.active[ep] = true;
		m.trb_pa[ep] = pa;
		R(0xc80c + 16 * ep) = cmd | ((ep + 1) << 16);
		break;
	}
	case 8:                         /* ENDTRANSFER: without GUCTL2[14] the end completes a little later */
		m.active[ep] = false;
		if (!(R(0xc19c) & (1u << 14))) {
			m.ending[ep] = 3;
		}
		break;
	case 2:                         /* SETTRANSFRESOURCE */
		if (ioc && m.cmdioc_mode == 1) {
			emit((2u << 24) | (7u << 6) | (ep << 1));
		} else if (ioc && m.cmdioc_mode == 2) {
			emit((ep << 1) | (1u << 6));
		} else if (ioc && m.cmdioc_mode == 3) {
			m.producer += 8;
			emit((2u << 24) | (7u << 6) | (ep << 1));
		}
		break;
	default:
		break;
	}
}

static void
mmio_wr(uint32_t pa, uint32_t v)
{
	m.mmio++;
	m.writes++;
	first_access(pa, true);
	if (pa >= QS && pa < QS + 0x100) {
		m.qs[(pa - QS) / 4] = v;
		m.qs_writes++;
		return;
	}
	if (!(pa >= DWC && pa < DWC + 0xd000)) {
		violation("write of unknown MMIO 0x%08x", pa);
		return;
	}
	uint32_t off = pa - DWC;
	switch (off) {
	case 0xc704:                    /* DCTL */
		R(off) = v;
		if (v & (1u << 31)) {
			uint64_t adr = ((uint64_t)R(0xc404) << 32) | R(0xc400);
			m.run_seen = true;
			if (host(adr, 4096) == NULL) {
				violation("RUN_STOP with the event buffer at 0x%llx", adr);
			}
			if (!m.run_stuck) {
				R(0xc70c) &= ~(1u << 22);
			}
		} else if (!m.halt_stuck) {
			R(0xc70c) |= 1u << 22;
		}
		return;
	case 0xc400: case 0xc404: case 0xc408:
		if (!m.gevnt_ignore) {
			R(off) = v;
		}
		return;
	case 0xc40c:
		R(off) -= v < R(off) ? v : R(off);
		return;
	default:
		break;
	}
	if (off >= 0xc800 && off < 0xc900 && (off & 0xf) == 0xc && (v & (1u << 10))) {
		command((off - 0xc80c) / 16, v);
		return;
	}
	R(off) = v;
}

static uint32_t dwc_rd(uint32_t off) { return mmio_rd(DWC + off); }
static void dwc_wr(uint32_t off, uint32_t v) { mmio_wr(DWC + off, v); }
static uint32_t ua_rd(vm_offset_t base, uint32_t off) { return mmio_rd((uint32_t)(base + off)); }
static void ua_wr(vm_offset_t addr, uint32_t v) { mmio_wr((uint32_t)addr, v); }
static void ud_rx_done(void) { if (ud.configured) { ud_rx_arm(); } }

/* ---- the host side ---- */
static void
reset(void)
{
	free(m.area);
	memset(&m, 0, sizeof(m));
	memset(&ud, 0, sizeof(ud));
	memset(&ua, 0, sizeof(ua));
	ud.bulk_mps = 512;
	ua.speed = UG_SPEED_HS;
	m.ready = true;
	m.cmdioc_mode = 1;
	m.fail_cmd = -1;
	R(0xc120) = 0x5533330a;         /* Phase 0 values */
	R(0xc110) = 0x00102001;
	R(0xc704) = 0x10f00000;
	R(0xc70c) = 0x00d212e4;
	R(0xc700) = 0x0048080c;
	R(0xc708) = 0x00001277;
	R(0xc720) = 0x0000000f;
	R(0xc400) = ABL_EVT;
	R(0xc408) = 0x80001000;
}

static void
fail_on(int cmd, int ep, int nth, int kind)
{
	m.fail_cmd = cmd; m.fail_ep = ep; m.fail_nth = nth; m.fail_kind = kind; m.fail_count = 0;
}

/* Run the event thread for up to n polls. Returns 0 still running, 1 it returned (stopped), 2 it panicked. */
static int
run(long n)
{
	m.budget = n;
	int r = setjmp(m.jmp);
	if (r == 0) {
		ua_thread(NULL, 0);
		return 1;
	}
	return r == 1 ? 0 : 2;
}

static int
gadget(int arg)
{
	int r = setjmp(m.jmp);
	if (r == 0) {
		ua_gadget(arg);
		return 0;
	}
	return r;
}

static bool
logged(const char *s)
{
	for (int i = 0; i < m.nlog; i++) {
		if (strstr(m.log[i], s)) {
			return true;
		}
	}
	return false;
}

static void
host_complete(unsigned ep, uint32_t left)
{
	uint32_t *t = host(m.trb_pa[ep], 16);
	t[2] = left;
	t[3] &= ~1u;
	m.active[ep] = false;
	emit((ep << 1) | (1u << 6));
}

/* One control transfer, as a host drives it, with the thread run between the steps. */
static void
control(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
	uint8_t st[8] = { type, req, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index, (uint8_t)(index >> 8),
		          (uint8_t)len, (uint8_t)(len >> 8) };
	if (!m.active[0] || ud.stopped) {
		return;
	}
	uint32_t *t = host(m.trb_pa[0], 16);
	memcpy(host(((uint64_t)t[1] << 32) | t[0], 8), st, 8);
	host_complete(0, 0);
	if (run(2) != 0) {
		return;
	}
	if (len && (type & 0x80) && m.active[1]) {
		host_complete(1, 0);
		run(2);
		emit((0u << 1) | (3u << 6) | (2u << 12));      /* status OUT on ep0 */
		run(2);
		if (m.active[0]) {
			host_complete(0, 0);
		}
	} else if (len && m.active[0]) {
		host_complete(0, 0);                            /* OUT data */
		run(2);
		emit((1u << 1) | (3u << 6) | (2u << 12));
		run(2);
		if (m.active[1]) {
			host_complete(1, 0);
		}
	} else if (!ud.stopped && !m.active[0]) {
		emit((1u << 1) | (3u << 6) | (2u << 12));      /* status IN on ep1 */
		run(2);
		if (m.active[1]) {
			host_complete(1, 0);
		}
	}
	run(2);
}

static void
enumerate(int level)
{
	emit(0x101);                    /* USB reset */
	emit(0x201);                    /* connect done, DSTS speed 0 = high */
	R(0xc70c) &= ~7u;
	run(2);
	control(0x80, 6, 0x0100, 0, 18);
	control(0x00, 5, 9, 0, 0);
	control(0x80, 6, 0x0200, 0, 255);
	control(0x00, 9, 1, 0, 0);
	if (level == 4) {
		control(0x01, 11, 1, 1, 0);
	}
}

/* ---- tests ---- */
static int fails, total;

static void
result(const char *name, bool ok)
{
	total++;
	fails += !ok;
	printf("%s", "");
	fprintf(stdout, "%-4s %-58s", ok ? "ok" : "FAIL", name);
	if (m.violations) {
		fprintf(stdout, " [%d violation(s): %s]", m.violations, m.viol[0]);
	}
	fputc('\n', stdout);
}

static bool clean(void) { return m.violations == 0 && m.panics == 0; }

static void
test_happy(void)
{
	reset();
	gadget(2);
	result("baseline: level 2 passes on the Phase 0 state", logged("L2: PASS") && !m.run_seen && clean());
	reset();
	gadget(3);
	enumerate(3);
	m.now += 21 * NSEC_PER_SEC;
	int r = run(5);
	result("baseline: level 3 enumerates, stops, passes", r == 1 && logged("L3: PASS") && clean() &&
	    (R(0xc70c) & (1u << 22)));
	reset();
	gadget(4);
	enumerate(4);
	result("baseline: level 4 configures and comes up", ud.configured && ud.data_alt == 1 && m.active[2] &&
	    m.active[5] && !ud.stopped && clean());
}

static void
test_f1(void)
{
	reset();
	m.ready = false;
	gadget(2);
	result("F1 recovery not armed: no MMIO, no allocation", m.mmio == 0 && m.area == NULL);
}

static void
test_f2(void)
{
	static const struct { int mode; const char *name; } c[] = {
		{ 0, "F2 no CmdIOC event: no PASS, no RUN_STOP, halted" },
		{ 2, "F2 an event of the wrong form: no PASS, no RUN_STOP" },
		{ 3, "F2 the event not at offset 0: no PASS, no RUN_STOP" },
	};
	for (unsigned i = 0; i < 3; i++) {
		reset();
		m.cmdioc_mode = c[i].mode;
		gadget(2);
		bool l2 = !logged("L2: PASS") && (R(0xc70c) & (1u << 22));
		reset();
		m.cmdioc_mode = c[i].mode;
		gadget(3);
		result(c[i].name, l2 && !m.run_seen && m.threads == 0);
	}
}

static void
test_f3(void)
{
	reset();
	R(0xc704) |= 1u << 31;          /* ABL left it running */
	R(0xc70c) &= ~(1u << 22);
	gadget(2);
	result("F3 core running at entry: refused with no write", m.writes == 0);

	reset();
	m.allocfail = true;
	gadget(2);
	result("F3 no DMA memory: no write before the area exists", m.writes == 0);

	reset();
	m.gevnt_ignore = true;
	gadget(2);
	result("F3 event address rejected: stop says UNVERIFIED, no PASS", logged("UNVERIFIED") &&
	    !logged("stays on our") && !logged("L2: PASS"));

	reset();
	gadget(3);
	enumerate(3);
	m.halt_stuck = true;
	m.now += 21 * NSEC_PER_SEC;
	int r = run(5);
	result("F3 core will not halt at stop: panic, no PASS", r == 2 && m.panics == 1 && !logged("L3: PASS"));

	reset();
	gadget(3);
	enumerate(3);
	fail_on(8, 0, 1, 1);            /* the stop's ENDTRANSFER of the SETUP transfer fails */
	m.now += 21 * NSEC_PER_SEC;
	run(5);
	result("F3 an ENDTRANSFER fails at stop: no L3 PASS", !logged("L3: PASS"));
}

static void
test_f4(void)
{
	reset();
	gadget(4);
	enumerate(4);
	emit(0x201);
	run(2);
	control(0x80, 6, 0x0100, 0, 18);                /* ep0 waits in its status stage, no status yet: */
	uint8_t st[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };
	uint32_t *t = host(m.trb_pa[0], 16);
	memcpy(host(((uint64_t)t[1] << 32) | t[0], 8), st, 8);
	host_complete(0, 0);
	run(2);
	host_complete(1, 0);
	run(2);                                         /* data done */
	emit((0u << 1) | (3u << 6) | (2u << 12));      /* status asked for: the status TRB on ep0 is now live */
	run(2);
	if (!m.active[0]) {
		violation("test setup: no live ep0 status transfer");
	}
	emit(0x101);                                    /* reset mid-transfer, RX and INT pending too */
	run(3);
	result("F4 reset with pending ep0 and bulk transfers: no reuse before end", clean() && !ud.stopped);

	reset();
	gadget(4);
	enumerate(4);
	int rx = m.starts[2];
	fail_on(8, 2, 1, 1);            /* ENDTRANSFER of RX fails on the reset */
	emit(0x101);
	run(5);
	result("F4 a failed ENDTRANSFER stops the gadget, slot never restarted", ud.stopped && m.starts[2] == rx &&
	    m.panics == 0);

	reset();
	gadget(4);
	enumerate(4);
	emit((2u << 1) | (1u << 6));    /* a completion whose TRB the controller still owns: stale */
	run(3);
	result("F4 a stale completion does not restart the slot", clean() && m.active[2] && !ud.stopped);
}

static void
test_f5(void)
{
	reset();
	gadget(4);
	enumerate(4);
	int n = m.starts[5];
	uint8_t before[16];
	memcpy(before, m.area + PAGE_SIZE + UD_NOTIFY, 16);
	control(0x01, 11, 1, 1, 0);     /* SET_INTERFACE(1, 1) again, the notification not yet read */
	control(0x01, 11, 1, 1, 0);
	result("F5 repeated SET_INTERFACE: one notification, buffer untouched", m.starts[5] == n && clean() &&
	    memcmp(before, m.area + PAGE_SIZE + UD_NOTIFY, 16) == 0);

	reset();
	gadget(4);
	enumerate(4);
	control(0x00, 9, 0, 0, 0);      /* SET_CONFIGURATION(0) */
	result("F5 SET_CONFIGURATION(0) unconfigures and ends data transfers", !ud.configured && !m.active[2] &&
	    !m.active[5] && clean());

	reset();
	gadget(4);
	enumerate(4);
	control(0x01, 11, 0, 1, 0);     /* SET_INTERFACE(1, 0) */
	result("F5 alternate 0 ends the data transfers", ud.data_alt == 0 && !m.active[2] && !m.active[5] && clean());
}

static void
test_f6(void)
{
	reset();
	fail_on(6, 0, 1, 1);            /* the first SETUP cannot be armed */
	gadget(3);
	result("F6 first SETUP fails: no VBUS override, no RUN_STOP", m.qs_writes == 0 && !m.run_seen);

	reset();
	gadget(4);
	fail_on(1, 2, 1, 1);            /* SETEPCONFIG of the bulk OUT endpoint fails */
	enumerate(4);
	run(3);
	result("F6 data endpoint commands fail: stopped, not configured", ud.stopped && ua.t_cfg == 0);

	reset();
	gadget(3);
	fail_on(1, 0, 1, 1);            /* connect done's ep0 reconfiguration fails */
	emit(0x201);
	run(3);
	result("F6 connect done reconfiguration fails: stopped", ud.stopped);

	reset();
	m.run_stuck = true;
	gadget(3);
	result("F6 RUN_STOP leaves the core halted: stopped, no thread", m.threads == 0 && ud.stopped);

	reset();
	gadget(4);
	enumerate(4);
	fail_on(6, 3, 1, 1);
	ud_start_trb(3, UD_TRB_TX, UD_TX, 100, TRBCTL_NORMAL);  /* what ud_tx_kick_locked does, under the lock */
	run(3);
	result("F6 a TX STARTTRANSFER fails: the thread stops the gadget", ud.stopped);
}

static void
test_f8(void)
{
	reset();
	gadget(2);
	bool l2 = m.unmarked == 0;
	reset();
	gadget(4);
	enumerate(4);
	m.now += 200 * NSEC_PER_SEC;
	run(3);
	bool l4 = m.unmarked == 0;
	if (!l2 || !l4) {
		fprintf(stdout, "     (%s)\n", m.first_unmarked);
	}
	result("F8 every first access to an unread register is announced", l2 && l4);
}

int
main(void)
{
	test_happy();
	test_f1();
	test_f2();
	test_f3();
	test_f4();
	test_f5();
	test_f6();
	test_f8();
	fprintf(stdout, "%s: %d tests, %d failed\n", fails ? "FAIL" : "PASS", total, fails);
	return fails ? 1 : 0;
}
