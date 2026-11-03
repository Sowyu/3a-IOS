/*
 * Host test for sweet_usb_gadget.h, the hardware-free logic of the asteroids gadget (patch 2160).
 *   cc -std=c11 -Wall -Wextra -Werror -I work/b/bsd/dev host/test_gadget.c -o /tmp/test_gadget && /tmp/test_gadget
 * Proves the logic against the USB 2.0/3.x descriptor rules and the DWC3 TRB and event arithmetic as written here.
 * It proves nothing about the hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_usb_gadget.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static const int levels[] = { UG_LEVEL_ENUM, UG_LEVEL_ECM };
static const int speeds[] = { UG_SPEED_HS, UG_SPEED_FS, UG_SPEED_SS };

/* Every DMA object on its own 64-byte lines, none overlapping, all inside the area. */
static void
test_layout(void)
{
	struct { const char *name; uint32_t off, size; } o[] = {
		{ "evt", UD_EVT, UD_EVT_SIZE }, { "trb out", UD_TRB_OUT, 16 }, { "trb in", UD_TRB_IN, 16 },
		{ "trb rx", UD_TRB_RX, 16 }, { "trb tx", UD_TRB_TX, 16 }, { "trb int", UD_TRB_INT, 16 },
		{ "setup", UD_SETUP, 8 }, { "notify", UD_NOTIFY, 16 }, { "data", UD_DATA, UD_DATA_SIZE },
		{ "rx", UD_RX, UD_FRAME_MAX }, { "tx", UD_TX, UD_FRAME_MAX },
	};
	int n = sizeof(o) / sizeof(o[0]);
	for (int i = 0; i < n; i++) {
		CHECK(o[i].off % 64 == 0, "%s not on a line", o[i].name);
		CHECK(o[i].off + o[i].size <= UD_SIZE, "%s past the area", o[i].name);
		for (int j = 0; j < i; j++) {
			uint32_t a0 = o[i].off / 64, a1 = (o[i].off + o[i].size - 1) / 64;
			uint32_t b0 = o[j].off / 64, b1 = (o[j].off + o[j].size - 1) / 64;
			CHECK(a1 < b0 || b1 < a0, "%s and %s share a cache line", o[i].name, o[j].name);
		}
	}
	CHECK(UD_TRB_OUT % 16 == 0 && UD_EVT_SIZE == 4096, "TRB alignment or event size");
	CHECK(UD_DATA_SIZE >= 512, "ep0 buffer smaller than one SuperSpeed ep0 packet");
}

static void
test_gates(void)
{
	CHECK(ug_trb_off_ok(UD_TRB_TX) && !ug_trb_off_ok(UD_TRB_TX + 16) && !ug_trb_off_ok(UD_SETUP), "trb slots");
	CHECK(ug_buf_off_ok(UD_SETUP, 8) && !ug_buf_off_ok(UD_SETUP, 9), "setup bounds");
	CHECK(ug_buf_off_ok(UD_DATA, 0) && ug_buf_off_ok(UD_DATA, 512) && !ug_buf_off_ok(UD_DATA, 513), "data bounds");
	CHECK(ug_buf_off_ok(UD_TX, 1515) && ug_buf_off_ok(UD_RX, UD_FRAME_MAX) && !ug_buf_off_ok(UD_RX, UD_FRAME_MAX + 1),
	    "frame bounds");
	CHECK(!ug_buf_off_ok(UD_EVT, 4) && !ug_buf_off_ok(UD_TRB_RX, 16), "event buffer and TRBs are not data buffers");
	CHECK(!ug_buf_off_ok(0xffffffffu, 2) && !ug_buf_off_ok(UD_TX + 1, 0xffffffffu), "wrap");
	CHECK(!ug_buf_off_ok(UD_SIZE, 0) && !ug_buf_off_ok(0x3000, 4), "outside every region");
	uint64_t b = 0x8c0000000ull;
	CHECK(ug_pa_ok(b, UD_SIZE, b, UD_SIZE) && !ug_pa_ok(b, UD_SIZE, b, UD_SIZE + 1), "pa whole area");
	CHECK(!ug_pa_ok(b, UD_SIZE, b - 1, 2) && !ug_pa_ok(b, UD_SIZE, b + UD_SIZE, 1), "pa edges");
	CHECK(!ug_pa_ok(b, UD_SIZE, ~0ull, 2) && !ug_pa_ok(b, UD_SIZE, b + 8, ~0ull), "pa wrap");
	/* every buffer start the kernel uses passes, with its real lengths */
	CHECK(ug_buf_off_ok(UD_NOTIFY, 16) && ug_buf_off_ok(UD_NOTIFY, 8), "notify");
}

static void
test_trb(void)
{
	uint32_t t[4];
	ug_trb_fill(t, 0x8c1234560ull, 0x3000000 | 2048, 0x813);
	CHECK(t[0] == 0xc1234560u && t[1] == 0x8 && t[2] == 2048 && t[3] == 0x813, "trb words");
}

/* Walk one configuration descriptor and check it against the spec rules the host enforces. */
static void
check_config(const uint8_t *c, uint32_t n, int level, int speed)
{
	CHECK(n >= 9 && c[0] == 9 && c[1] == 2, "config header");
	CHECK((uint32_t)(c[2] | c[3] << 8) == n, "wTotalLength %u vs %u", c[2] | c[3] << 8, n);
	int interfaces = 0, want_eps = 0, eps = 0, last_alt0 = -1;
	uint32_t i = 9;
	while (i < n) {
		CHECK(c[i] >= 2 && i + c[i] <= n, "descriptor at %u overruns", i);
		if (c[i] < 2 || i + c[i] > n) {
			return;
		}
		if (c[i + 1] == 4) {
			CHECK(eps == want_eps, "interface before %u: %d endpoints, declared %d", i, eps, want_eps);
			if (c[i + 3] == 0 && c[i + 2] != last_alt0) {
				interfaces++;
				last_alt0 = c[i + 2];
			}
			want_eps = c[i + 4];
			eps = 0;
		} else if (c[i + 1] == 5) {
			uint16_t mps = (uint16_t)(c[i + 4] | c[i + 5] << 8);
			eps++;
			if ((c[i + 3] & 3) == 2) {
				CHECK(mps == ug_bulk_mps(speed), "bulk mps %u at speed %d", mps, speed);
			}
			bool comp = i + c[i] < n && c[i + c[i] + 1] == 0x30;
			CHECK(comp == (speed == UG_SPEED_SS), "companion after endpoint 0x%02x at speed %d", c[i + 2], speed);
			if (comp) {
				const uint8_t *k = c + i + c[i];
				CHECK(k[0] == 6 && k[2] == 0, "companion shape");
				CHECK((uint32_t)(k[4] | k[5] << 8) == ((c[i + 3] & 3) == 3 ? mps : 0), "wBytesPerInterval");
			}
		} else if (c[i + 1] == 0x30) {
			CHECK(i >= 7 && c[i - 7 + 1] == 5, "companion not right after an endpoint");
		}
		i += c[i];
	}
	CHECK(eps == want_eps, "last interface endpoints");
	CHECK(interfaces == c[4], "bNumInterfaces %u vs %d", c[4], interfaces);
	CHECK(speed == UG_SPEED_SS ? c[8] * 8 <= 900 : c[8] * 2 <= 500, "bMaxPower");
	CHECK(level == UG_LEVEL_ECM ? c[4] == 2 : (c[4] == 1 && n == 18), "interfaces per level");
}

static void
test_descriptors(void)
{
	uint8_t out[UD_DATA_SIZE];
	for (unsigned l = 0; l < 2; l++) {
		for (unsigned s = 0; s < 3; s++) {
			int level = levels[l], speed = speeds[s];
			uint32_t n = ug_descriptor(level, speed, 0x0100, out);
			CHECK(n == 18 && out[0] == 18 && out[1] == 1, "device");
			CHECK(out[7] == (speed == UG_SPEED_SS ? 9 : 64), "bMaxPacketSize0");
			CHECK((out[2] | out[3] << 8) == (speed == UG_SPEED_SS ? 0x0300 : 0x0200), "bcdUSB");
#ifdef UG_BCD_DEVICE
			CHECK((out[12] | out[13] << 8) == UG_BCD_DEVICE, "one bcdDevice at every level (v4)");
			CHECK(out[4] == 0 && out[5] == 0 && out[6] == 0, "class 0/0/0 at every level (v4)");
#else
			CHECK((out[12] | out[13] << 8) == 0x0100 + level, "bcdDevice names the level");
			CHECK(out[4] == (level == UG_LEVEL_ECM ? 2 : 0), "device class");
#endif
			CHECK(ug_descriptor(level, speed, out[15] | 0x0300, out + 18) > 2, "product string exists");
			n = ug_descriptor(level, speed, 0x0200, out);
			if (level == UG_LEVEL_ECM && speed == UG_SPEED_FS) {
				CHECK(n == 0, "no full-speed ECM");
			} else {
				check_config(out, n, level, speed);
			}
			CHECK(ug_descriptor(level, speed, 0x0201, out) == 0, "config index 1 stalls");
			n = ug_descriptor(level, speed, 0x0f00, out);
			CHECK(speed == UG_SPEED_SS ? n == 22 : n == 0, "BOS only at SuperSpeed");
			if (n) {
				CHECK(out[0] == 5 && out[1] == 15 && (uint32_t)(out[2] | out[3] << 8) == n && out[4] == 2, "BOS header");
				CHECK((uint32_t)(out[5] + out[5 + out[5]] + 5) == n, "BOS caps add up");
			}
			CHECK(ug_descriptor(level, speed, 0x0600, out) == 0 && ug_descriptor(level, speed, 0x0700, out) == 0,
			    "qualifier and other speed stall");
		}
	}
	for (unsigned i = 0; i < 8; i++) {
		uint32_t n = ug_string(i, out);
		if (i == 0) {
			CHECK(n == 4 && out[0] == 4 && out[1] == 3, "langid");
		} else if (i < sizeof(ug_strings) / sizeof(ug_strings[0])) {
			CHECK(n == out[0] && n % 2 == 0 && n <= 255 && out[1] == 3, "string %u", i);
		} else {
			CHECK(n == 0, "missing string %u stalls", i);
		}
	}
	/* The ECM MAC string is 12 hex digits (CDC ECM 1.2, 5.4). */
	CHECK(ug_string(4, out) == 26, "iMACAddress length");
}

static struct ug_plan
req(int level, int speed, bool configured, uint8_t type, uint8_t r, uint16_t value, uint16_t index, uint16_t len,
    uint8_t *data)
{
	uint8_t st[8] = { type, r, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index, (uint8_t)(index >> 8),
		          (uint8_t)len, (uint8_t)(len >> 8) };
	struct ug_ctx c = { .level = level, .speed = speed, .configured = configured, .data_alt = 0 };
#ifdef UG_CTX_ADDRESSED
	c.addressed = true;
#endif
	return ug_setup_plan(st, &c, data);
}

static void
test_setup(void)
{
	uint8_t d[UD_DATA_SIZE];
	struct ug_plan p;

	for (unsigned l = 0; l < 2; l++) {
		for (unsigned s = 0; s < 3; s++) {
			int L = levels[l], S = speeds[s];
			uint32_t mps0 = ug_mps0(S);
			/* No IN reply may end on a full packet shorter than asked: that would need a zero-length packet. */
			for (uint16_t v = 0x0100; v <= 0x0f00; v += 0x0100) {
				for (uint16_t idx = 0; idx < 8; idx++) {
					for (uint32_t wlen = 1; wlen <= 0xffff; wlen = wlen * 2 + 1) {
						p = req(L, S, false, 0x80, 6, (uint16_t)(v | (v == 0x0300 ? idx : 0)), 0, (uint16_t)wlen, d);
						if (p.stage == UG_IN) {
							CHECK(p.len <= wlen && p.len <= UD_DATA_SIZE, "IN length");
							CHECK(!(p.len < wlen && p.len % mps0 == 0), "would need a ZLP: desc 0x%04x len %u", v, p.len);
						}
					}
				}
			}
			p = req(L, S, false, 0x80, 6, 0x0100, 0, 64, d);
			CHECK(p.stage == UG_IN && p.len == 18, "device descriptor");
			p = req(L, S, false, 0x80, 6, 0x0100, 0, 8, d);
			CHECK(p.stage == UG_IN && p.len == 8, "first 8 bytes");
			p = req(L, S, false, 0x00, 5, 7, 0, 0, d);
			CHECK(p.stage == UG_NODATA && p.action == UG_ACT_ADDRESS && p.arg == 7, "SET_ADDRESS");
			p = req(L, S, false, 0x00, 5, 128, 0, 0, d);
			CHECK(p.stage == UG_STALL, "address 128");
			p = req(L, S, false, 0x00, 9, 1, 0, 0, d);
			if (L == UG_LEVEL_ECM && S == UG_SPEED_FS) {
				CHECK(p.stage == UG_STALL, "no ECM config at full speed");
			} else {
				CHECK(p.stage == UG_NODATA && p.action == UG_ACT_CONFIG && p.arg == 1, "SET_CONFIGURATION 1");
			}
			p = req(L, S, false, 0x00, 9, 2, 0, 0, d);
			CHECK(p.stage == UG_STALL, "config 2");
			p = req(L, S, true, 0x80, 8, 0, 0, 1, d);
			CHECK(p.stage == UG_IN && p.len == 1 && d[0] == 1, "GET_CONFIGURATION");
			p = req(L, S, true, 0x01, 11, 1, 1, 0, d);
			CHECK(L == UG_LEVEL_ECM ? (p.action == UG_ACT_ALT && p.arg == 1 && p.stage == UG_NODATA) : p.stage == UG_STALL,
			    "SET_INTERFACE 1/1");
			p = req(L, S, true, 0x01, 11, 0, 0, 0, d);
			CHECK(p.stage == UG_NODATA && p.action == UG_ACT_NONE, "SET_INTERFACE 0/0");
			p = req(L, S, true, 0x01, 11, 2, 1, 0, d);
			CHECK(p.stage == UG_STALL, "alt 2");
			p = req(L, S, true, 0x21, 0x43, 0x000e, 0, 0, d);        /* SET_ETHERNET_PACKET_FILTER */
			CHECK(L == UG_LEVEL_ECM ? p.stage == UG_NODATA : p.stage == UG_STALL, "packet filter");
			p = req(L, S, true, 0xa1, 0x44, 0, 0, 4, d);            /* class request with data */
			CHECK(p.stage == UG_STALL, "class with data stalls");
			p = req(L, S, true, 0x40, 1, 0, 0, 0, d);
			CHECK(p.stage == UG_STALL, "vendor stalls");
			p = req(L, S, true, 0x00, 48, 0, 0, 6, d);              /* SET_SEL */
			CHECK(p.stage == UG_OUT && p.len == mps0 && p.len <= UD_DATA_SIZE && p.len % mps0 == 0, "SET_SEL OUT");
			p = req(L, S, true, 0x00, 48, 0, 0, 5, d);
			CHECK(p.stage == UG_STALL, "SET_SEL length");
			p = req(L, S, true, 0x00, 49, 40, 0, 0, d);
			CHECK(p.stage == UG_NODATA, "SET_ISOCH_DELAY");
			p = req(L, S, true, 0x02, 1, 0, 0x81, 0, d);            /* CLEAR_FEATURE(ENDPOINT_HALT) */
			CHECK(p.stage == UG_NODATA && p.action == UG_ACT_NONE, "CLEAR_FEATURE");
			p = req(L, S, true, 0x80, 0, 0, 0, 2, d);
			CHECK(p.stage == UG_IN && p.len == 2 && d[0] == 0 && d[1] == 0, "GET_STATUS");
			p = req(L, S, true, 0x00, 7, 0x0100, 0, 18, d);         /* SET_DESCRIPTOR */
			CHECK(p.stage == UG_STALL, "SET_DESCRIPTOR stalls");
			p = req(L, S, true, 0x00, 1, 0, 0, 1024, d);            /* OUT bigger than one ep0 buffer */
			CHECK(p.stage == UG_STALL && p.action == UG_ACT_NONE, "oversize OUT stalls");
		}
	}
}

static void
test_frames(void)
{
	uint32_t mpss[] = { 512, 1024 };
	for (unsigned m = 0; m < 2; m++) {
		for (uint32_t len = 14; len <= 1514; len++) {
			uint32_t t = ug_tx_len(len, mpss[m]);
			CHECK(t % mpss[m] != 0 && t <= UD_FRAME_MAX && t >= len && t <= len + 1, "tx %u mps %u", len, mpss[m]);
			/* the host's receiver: packets of mps, ended by a short one; what it gets back is the frame plus at most a pad */
			uint32_t got = 0, left = t;
			while (left >= mpss[m]) {
				got += mpss[m];
				left -= mpss[m];
			}
			got += left;
			CHECK(got == t && left != 0, "frame %u ends on a short packet", len);
			/* rx: the TRB's remaining size after a frame of len into the 2 KiB buffer */
			CHECK(ug_rx_len(UD_FRAME_MAX - len, UD_FRAME_MAX) == len, "rx %u", len);
		}
	}
	CHECK(ug_rx_len(UD_FRAME_MAX + 1, UD_FRAME_MAX) == 0, "impossible remaining size reads as 0");
	CHECK(ug_rx_len(0xff000000u | (UD_FRAME_MAX - 60), UD_FRAME_MAX) == 60, "upper TRB bits ignored");
}

/* The controller writes count bytes of events at its pointer; the reader follows with ug_evt_next. */
static void
test_events(void)
{
	CHECK(ug_evt_count_ok(4) && ug_evt_count_ok(4096) && !ug_evt_count_ok(4100) && !ug_evt_count_ok(6), "counts");
	CHECK(ug_evt_next(4092) == 0 && ug_evt_next(0) == 4, "wrap");
	uint32_t w = 40, r = 40, seed = 1;          /* start where the CmdIOC self-test left the pointer */
	for (int i = 0; i < 100000; i++) {
		seed = seed * 1103515245u + 12345u;
		uint32_t n = ((seed >> 16) % 64) * 4;
		w = (w + n) % UD_EVT_SIZE;
		for (uint32_t done = 0; done < n; done += 4) {
			r = ug_evt_next(r);
		}
		if (r != w) {
			CHECK(0, "reader %u writer %u after %d batches", r, w, i);
			break;
		}
	}
	checks++;
}

/* v2: the CmdIOC proof, one slot per endpoint, and requests checked by direction, recipient and state. */
static void
test_v2(void)
{
	static uint32_t evt[UD_EVT_SIZE / 4];
	uint8_t d[UD_DATA_SIZE];
	struct ug_plan p;

	memset(evt, 0, sizeof(evt));
	evt[0] = 0x020001c2u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_OK, "the expected event");
	evt[0] = 0xa2ff01c2u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_OK, "bits 23:16 and 31:28 are not checked");
	CHECK(ug_cmdioc_proof(evt, 0) == UG_PROOF_COUNT && ug_cmdioc_proof(evt, 8) == UG_PROOF_COUNT, "count must be 4");
	evt[0] = 0x020001c0u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_FORM, "ep0 instead of ep1");
	evt[0] = 0x03000042u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_FORM, "XferComplete instead of EPCMDCMPLT");
	evt[0] = 0x020011c2u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_FORM, "non-zero status");
	evt[0] = 0;
	evt[2] = 0x020001c2u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_FORM, "the event at offset 8");
	evt[0] = 0x020001c2u;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_STRAY, "a second word written");
	evt[2] = 0;
	evt[1023] = 1;
	CHECK(ug_cmdioc_proof(evt, 4) == UG_PROOF_STRAY, "a word written at the end");

	uint32_t seen[6] = { 0 };
	for (unsigned ep = 0; ep < 6; ep++) {
		uint32_t t = ug_trb_slot(ep);
		CHECK(t == 0 || ug_trb_off_ok(t), "slot %u is a TRB slot", ep);
		for (unsigned j = 0; j < ep; j++) {
			CHECK(t == 0 || t != seen[j], "slot shared by ep%u and ep%u", j, ep);
		}
		seen[ep] = t;
	}
	CHECK(ug_trb_slot(4) == 0 && ug_trb_slot(6) == 0, "unused endpoints have no slot");

	p = req(UG_LEVEL_ECM, UG_SPEED_HS, true, 0x00, 5, 3, 0, 0, d);
	CHECK(p.stage == UG_STALL, "SET_ADDRESS once configured stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, false, 0x00, 6, 0x0100, 0, 18, d);
	CHECK(p.stage == UG_STALL, "GET_DESCRIPTOR in the OUT direction stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, false, 0x81, 6, 0x0100, 0, 18, d);
	CHECK(p.stage == UG_STALL, "GET_DESCRIPTOR to an interface stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, false, 0x01, 11, 1, 1, 0, d);
	CHECK(p.stage == UG_STALL, "SET_INTERFACE before configuration stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, true, 0x00, 11, 1, 1, 0, d);
	CHECK(p.stage == UG_STALL, "SET_INTERFACE to the device recipient stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, true, 0x80, 9, 1, 0, 0, d);
	CHECK(p.stage == UG_STALL, "SET_CONFIGURATION in the IN direction stalls");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, true, 0x81, 10, 0, 1, 1, d);
	CHECK(p.stage == UG_IN && p.len == 1, "GET_INTERFACE");
	p = req(UG_LEVEL_ECM, UG_SPEED_HS, true, 0x83, 0, 0, 0, 2, d);
	CHECK(p.stage == UG_STALL, "GET_STATUS to recipient 3 stalls");
}

/* v3: the whole request shape (re-audit finding 3). These fail on the v2 header. */
static struct ug_plan
req_at(int level, bool addressed, bool configured, uint8_t type, uint8_t r, uint16_t value, uint16_t index,
    uint16_t len, uint8_t *data)
{
	uint8_t st[8] = { type, r, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index, (uint8_t)(index >> 8),
		          (uint8_t)len, (uint8_t)(len >> 8) };
	struct ug_ctx c = { .level = level, .speed = UG_SPEED_HS, .configured = configured, .data_alt = 0 };
#ifdef UG_CTX_ADDRESSED
	c.addressed = addressed;
#else
	(void)addressed;
#endif
	return ug_setup_plan(st, &c, data);
}

static void
test_v3(void)
{
	uint8_t d[UD_DATA_SIZE];
	const int E = UG_LEVEL_ECM;

	CHECK(req_at(E, true, false, 0x00, 9, 1, 0, 1, d).stage == UG_STALL, "SET_CONFIGURATION with wLength 1 stalls");
	CHECK(req_at(E, true, false, 0x00, 9, 1, 0, 1, d).action == UG_ACT_NONE, "... and configures nothing");
	CHECK(req_at(E, false, false, 0x00, 9, 1, 0, 0, d).stage == UG_STALL, "SET_CONFIGURATION in the Default state");
	CHECK(req_at(E, true, false, 0x00, 9, 1, 3, 0, d).stage == UG_STALL, "SET_CONFIGURATION with wIndex 3");
	CHECK(req_at(E, true, false, 0x00, 9, 1, 0, 0, d).action == UG_ACT_CONFIG, "a well-formed SET_CONFIGURATION");
	CHECK(req_at(E, true, true, 0x01, 11, 1, 1, 2, d).stage == UG_STALL, "SET_INTERFACE with wLength 2");
	CHECK(req_at(E, true, true, 0x01, 11, 0, 2, 0, d).stage == UG_STALL, "SET_INTERFACE to interface 2");
	CHECK(req_at(UG_LEVEL_ENUM, true, true, 0x01, 11, 0, 1, 0, d).stage == UG_STALL, "level 3 has no interface 1");
	CHECK(req_at(E, true, true, 0x81, 10, 0, 5, 1, d).stage == UG_STALL, "GET_INTERFACE of interface 5");
	CHECK(req_at(E, true, true, 0x81, 10, 0, 1, 1, d).stage == UG_IN, "GET_INTERFACE of interface 1");
	CHECK(req_at(E, true, false, 0x21, 0x43, 0x0e, 0, 0, d).stage == UG_STALL, "packet filter before configuration");
	CHECK(req_at(E, true, true, 0xa1, 0x43, 0x0e, 0, 0, d).stage == UG_STALL, "IN class request");
	CHECK(req_at(E, true, true, 0x20, 0x43, 0x0e, 0, 0, d).stage == UG_STALL, "class request to the device");
	CHECK(req_at(E, true, true, 0x21, 0x43, 0x0e, 1, 0, d).stage == UG_STALL, "packet filter to the data interface");
	CHECK(req_at(E, true, true, 0x21, 0x44, 0, 0, 0, d).stage == UG_STALL, "another class request");
	CHECK(req_at(E, true, true, 0x21, 0x43, 0x0e, 0, 0, d).stage == UG_NODATA, "the packet filter itself");
	CHECK(req_at(E, true, false, 0x00, 5, 3, 1, 0, d).stage == UG_STALL, "SET_ADDRESS with wIndex 1");
	CHECK(req_at(E, true, false, 0x80, 0, 0, 0, 4, d).stage == UG_STALL, "GET_STATUS with wLength 4");
	CHECK(req_at(E, true, false, 0x00, 1, 0, 0, 2, d).stage == UG_STALL, "CLEAR_FEATURE with data");
	CHECK(req_at(E, true, false, 0x80, 8, 0, 0, 1, d).stage == UG_IN, "GET_CONFIGURATION");
	CHECK(req_at(E, false, false, 0x80, 8, 0, 0, 1, d).stage == UG_STALL, "GET_CONFIGURATION in the Default state");
}

/*
 * v4: the identity a Mac's accessory approval is cached against is the same at levels 3 and 4: the device descriptor
 * and every string it names. Class 0/0/0 at the device is what AppleUSBHostCompositeDevice matches
 * (IOUSBHostFamily.kext/.../AppleUSBHostCompositeDevice.kext Info.plist on this Mac), and the ECM interfaces carry the
 * classes AppleUserECM matches (2/6 and 10/0, AppleUserECM.dext Info.plist). These fail on the v3 header.
 */
static void
test_v4(void)
{
	uint8_t a[UD_DATA_SIZE], b[UD_DATA_SIZE], c[UD_DATA_SIZE];
	for (unsigned s = 0; s < 3; s++) {
		uint32_t na = ug_descriptor(UG_LEVEL_ENUM, speeds[s], 0x0100, a);
		uint32_t nb = ug_descriptor(UG_LEVEL_ECM, speeds[s], 0x0100, b);
		CHECK(na == nb && memcmp(a, b, na) == 0, "the same device descriptor at levels 3 and 4, speed %d", speeds[s]);
		CHECK(a[4] == 0 && a[5] == 0, "AppleUSBHostCompositeDevice matches bDeviceClass 0, bDeviceSubClass 0");
		for (unsigned k = 14; k <= 16; k++) {
			if (a[k] == 0) {
				continue;
			}
			uint32_t sa = ug_descriptor(UG_LEVEL_ENUM, speeds[s], (uint16_t)(0x0300 | a[k]), b);
			uint32_t sb = ug_descriptor(UG_LEVEL_ECM, speeds[s], (uint16_t)(0x0300 | a[k]), c);
			CHECK(sa > 2 && sa == sb && memcmp(b, c, sa) == 0, "string %u the same at levels 3 and 4", a[k]);
		}
	}
	uint32_t n = ug_descriptor(UG_LEVEL_ECM, UG_SPEED_HS, 0x0200, a);
	int comm = 0, data = 0;
	for (uint32_t i = 0; i < n; i += a[i]) {
		if (a[i + 1] == 4 && a[i + 5] == 2 && a[i + 6] == 6) {
			comm++;
		}
		if (a[i + 1] == 4 && a[i + 5] == 10 && a[i + 6] == 0 && a[i + 7] == 0) {
			data++;
		}
	}
	CHECK(comm == 1 && data == 2, "AppleUserECM's interfaces: one 2/6, data 10/0 in both alternates");
	n = ug_descriptor(UG_LEVEL_ENUM, UG_SPEED_HS, 0x0200, a);
	CHECK(n == 18 && a[9 + 5] == 0xff && a[9 + 4] == 0, "level 3: one vendor interface, no endpoints");
}

static void
test_canary(void)
{
	static uint32_t page[4096];
	for (uint32_t i = 0; i < 4096; i++) {
		page[i] = ug_canary(i);
	}
	CHECK(ug_canary_check(page, 4096) == -1, "clean page");
	page[1234] ^= 1;
	CHECK(ug_canary_check(page, 4096) == 1234, "one flipped bit found");
	page[1234] ^= 1;
	page[0] = 0;
	CHECK(ug_canary_check(page, 4096) == 0, "zero write found");
	int distinct = 1;
	for (uint32_t i = 1; i < 4096 && distinct; i++) {
		distinct = ug_canary(i) != ug_canary(i - 1) && ug_canary(i) != 0;
	}
	CHECK(distinct, "pattern varies and is never 0");
}

int
main(void)
{
	test_layout();
	test_gates();
	test_trb();
	test_descriptors();
	test_setup();
	test_frames();
	test_events();
	test_canary();
	test_v2();
	test_v3();
	test_v4();
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
	return fails ? 1 : 0;
}
