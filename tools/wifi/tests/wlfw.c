/* Host tests for the WLFW QMI encoding and decoding (2248), pinned to bytes worked out by hand from the documented layout.
 *
 * The layout, stated once here so every expected byte below can be checked against it without reading our code:
 *
 *   A QMI version-1 message is a 7-byte header and then type-length-value triples.
 *     byte 0      type: 0 request, 2 response, 4 indication
 *     bytes 1-2   transaction, little-endian
 *     bytes 3-4   message id, little-endian
 *     bytes 5-6   payload length (everything after the header), little-endian
 *     then, per TLV: type (1), length (2, little-endian), value (length bytes, no padding)
 *
 *   IND_REGISTER request, id 0x0020. TLV 0x10 u8 fw_ready_enable = 1; TLV 0x15 u32 client_id = 0x4b4e454c ("LENK");
 *   TLV 0x18 u8 fw_init_done_enable = 1; TLV 0x1B u8 cal_done_enable = 1. Nothing else, because this chip has fixed
 *   firmware memory. Length: 4 + 7 + 4 + 4 = 19; whole message 7 + 19 = 26.
 *
 *   HOST_CAP request, id 0x0034. TLV 0x10 u32 num_clients = 1; TLV 0x14 u8 bdf_support = 1; TLV 0x1A u8 cal_done = 0;
 *   TLV 0x1C u8 mem_cfg_mode = 0. Length: 7 + 4 + 4 + 4 = 19; whole message 26. cal_done is 0x1A in this message: in
 *   host_cap 0x1B is mem_bucket (a u32) and putting a byte there would be a different message.
 *
 *   CAP request, id 0x0024, and DEVICE_INFO request, id 0x004C: no TLV at all, so 7 bytes each.
 *
 *   The CAP response's board id is TLV 0x11, u32; the chip identity is TLV 0x10, two u32s (chip, family); the
 *   DEVICE_INFO response's register window is TLV 0x10 u64 (address) and TLV 0x11 u32 (size). The result TLV is 0x02:
 *   u16 result then u16 error, 4 bytes.
 *
 * Usage: wlfw-tests            (no arguments, no files, no hardware) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "WlfwQmi.h"

static int checks, failures;
static void check(int ok, const char *what)
{
    checks++;
    if (ok) return;
    failures++;
    printf("FAIL: %s\n", what);
}
/* Little-endian readers for the checks that assert single fields rather than whole buffers. */
static uint32_t get16(const uint8_t *p) {return (uint32_t)p[0] | (uint32_t)p[1] << 8;}
static uint32_t get32(const uint8_t *p) {return get16(p) | get16(p + 2) << 16;}

/* Byte-exact comparison, printing both sides when they differ. */static void check_bytes(const char *what, const uint8_t *got, uint32_t got_length,
                        const uint8_t *want, uint32_t want_length)
{
    int same = got_length == want_length && memcmp(got, want, want_length) == 0;
    check(same, what);
    if (same) return;
    printf("  got  (%u):", got_length);
    for (uint32_t i = 0; i < got_length && i < 64; i++) printf(" %02x", got[i]);
    printf("\n  want (%u):", want_length);
    for (uint32_t i = 0; i < want_length && i < 64; i++) printf(" %02x", want[i]);
    printf("\n");
}

/* ---- the encoders, against bytes worked out by hand ------------------------------------------------ */

static void t_ind_register(void)
{
    uint8_t out[64];
    /* 7 header + 4 (T) + 7 (client) + 4 (init_done) + 4 (cal_done) = 26 */
    static const uint8_t want[] = {
        0x00, 0x07, 0x00, 0x20, 0x00, 0x13, 0x00,             /* request, transaction 7, id 0x0020, 19 bytes */
        0x10, 0x01, 0x00, 0x01,                               /* fw_ready_enable = 1 */
        0x15, 0x04, 0x00, 0x4c, 0x45, 0x4e, 0x4b,             /* client_id = "LENK" little-endian */
        0x18, 0x01, 0x00, 0x01,                               /* fw_init_done_enable = 1 */
        0x1b, 0x01, 0x00, 0x01,                               /* cal_done_enable = 1 */
    };
    const uint32_t n = wlfw_ind_register(out, sizeof out, 7);
    check(n == 26, "ind_register: 26 bytes");
    check_bytes("ind_register: the exact bytes", out, n, want, sizeof want);
    /* The client id is 0x4b4e454c, the four bytes of "LENK" as ath11k defines it. */
    check(wlfw_get32(out + 14) == 0x4b4e454cu, "ind_register: client id 0x4b4e454c");
    /* Nothing about memory: no 0x16 (request_mem) and no 0x17 (fw_mem_ready), because this chip's is fixed. */
    int saw_mem = 0;
    for (uint32_t at = 7; at < n;) {
        const uint32_t len = wlfw_get16(out + at + 1);
        if (out[at] == 0x16 || out[at] == 0x17) saw_mem = 1;
        at += 3 + len;
    }
    check(!saw_mem, "ind_register: no request_mem and no fw_mem_ready");
    /* A buffer one byte too small is refused, not truncated. */
    check(wlfw_ind_register(out, 25, 7) == 0, "ind_register: a 25-byte buffer is refused");
}

static void t_host_cap(void)
{
    uint8_t out[64];
    static const uint8_t want[] = {
        0x00, 0x01, 0x00, 0x34, 0x00, 0x13, 0x00,             /* request, transaction 1, id 0x0034, 19 bytes */
        0x10, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,             /* num_clients = 1 */
        0x14, 0x01, 0x00, 0x01,                               /* bdf_support = 1 */
        0x1a, 0x01, 0x00, 0x00,                               /* cal_done = 0 */
        0x1c, 0x01, 0x00, 0x00,                               /* mem_cfg_mode = 0 */
    };
    const uint32_t n = wlfw_host_cap(out, sizeof out, 1);
    check(n == 26, "host_cap: 26 bytes");
    check_bytes("host_cap: the exact bytes", out, n, want, sizeof want);
    /* cal_done rides 0x1A here, and 0x1B must not appear: in this message 0x1B is a u32 mem_bucket. */
    int saw_1b = 0;
    for (uint32_t at = 7; at < n;) {
        const uint32_t len = wlfw_get16(out + at + 1);
        if (out[at] == 0x1b) saw_1b = 1;
        at += 3 + len;
    }
    check(!saw_1b, "host_cap: no TLV 0x1B (that is mem_bucket here, not cal_done)");
}

static void t_empty(void)
{
    uint8_t out[16];
    static const uint8_t cap[] = {0x00, 0x02, 0x00, 0x24, 0x00, 0x00, 0x00};
    static const uint8_t dev[] = {0x00, 0x03, 0x00, 0x4c, 0x00, 0x00, 0x00};
    uint32_t n = wlfw_empty_request(out, sizeof out, 2, WLFW_CAP_REQ);
    check(n == 7, "cap: 7 bytes");
    check_bytes("cap: header only", out, n, cap, sizeof cap);
    n = wlfw_empty_request(out, sizeof out, 3, WLFW_DEVICE_INFO_REQ);
    check(n == 7, "device_info: 7 bytes");
    check_bytes("device_info: header only", out, n, dev, sizeof dev);
    check(wlfw_empty_request(out, 6, 1, WLFW_CAP_REQ) == 0, "cap: a 6-byte buffer is refused");
}

/* ---- the decoders, against bytes worked out by hand ------------------------------------------------ */

static void t_parse(void)
{
    /* A response to transaction 4 for DEVICE_INFO carrying the result and the window, built by hand. */
    static const uint8_t reply[] = {
        0x02, 0x04, 0x00, 0x4c, 0x00, 0x19, 0x00,             /* response, transaction 4, id 0x004C, 25 bytes */
        0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,             /* result 0, error 0 */
        0x10, 0x08, 0x00, 0x00, 0x00, 0xad, 0x0b, 0x00, 0x00, 0x00, 0x00,   /* bar_addr 0x0bad0000: a made-up value, not a map address */
        0x11, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00,             /* bar_size 0x200000 */
    };
    WlfwMessage m;
    check(wlfw_parse(reply, sizeof reply, &m) == 0, "parse: a well-formed reply is accepted");
    check(m.type == WLFW_RESPONSE && m.transaction == 4 && m.message == 0x004c, "parse: header fields");
    check(m.result_valid && m.result == 0 && m.error == 0, "parse: result 0 error 0");
    uint64_t addr = 0;
    uint32_t size = 0;
    check(wlfw_device_info(reply, sizeof reply, &addr, &size) == 0, "device_info: both TLVs found");
    check(addr == 0x0bad0000ull && size == 0x200000u, "device_info: address and size decoded");

    uint32_t board = 0, chip = 0, family = 0;
    /* A CAP reply with chip 1 family 0xb, board 0xff, soc 0x1234. Payload: 7 (result) + 11 (chip) + 7 (board) +
     * 7 (soc) = 32 bytes, so the header's length is 0x20 and the message is 39. */
    static const uint8_t cap[] = {
        0x02, 0x03, 0x00, 0x24, 0x00, 0x20, 0x00,
        0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
        0x11, 0x04, 0x00, 0xff, 0x00, 0x00, 0x00,
        0x12, 0x04, 0x00, 0x34, 0x12, 0x00, 0x00,
    };
    check(wlfw_parse(cap, sizeof cap, &m) == 0 && m.message == 0x0024, "parse: a cap reply");
    check(wlfw_board_id(cap, sizeof cap, &board) == 0 && board == 0xff, "cap: board id 0xff");
    check(wlfw_chip_info(cap, sizeof cap, &chip, &family) == 0 && chip == 1 && family == 0xb, "cap: chip 1 family 0xb");
    /* A cap reply without a board id says so rather than inventing one. Payload 18 bytes. */
    static const uint8_t noboard[] = {
        0x02, 0x03, 0x00, 0x24, 0x00, 0x12, 0x00,
        0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    };
    check(wlfw_board_id(noboard, sizeof noboard, &board) == 1, "cap: no board id is reported as absent");

    /* A refusal: result 1, error 48 (0x30). Payload: the result TLV alone, 7 bytes. */
    static const uint8_t refused[] = {
        0x02, 0x01, 0x00, 0x20, 0x00, 0x07, 0x00,
        0x02, 0x04, 0x00, 0x01, 0x00, 0x30, 0x00,
    };
    check(wlfw_parse(refused, sizeof refused, &m) == 0, "parse: a refusal");
    check(m.result_valid && m.result == 1 && m.error == 0x30, "parse: refusal result 1 error 48");

    /* Everything malformed must be rejected, not half-read: a length that disagrees with the bytes present, a TLV that
     * claims more than the message holds, a short message, and a result TLV of the wrong size. */
    uint8_t bad[32];
    memcpy(bad, reply, sizeof reply);
    bad[5] = 0x1a;                                   /* one byte more than is there */
    check(wlfw_parse(bad, sizeof reply, &m) != 0, "parse: a length past the bytes is refused");
    memcpy(bad, reply, sizeof reply);
    bad[7 + 1] = 0x09;                               /* the result TLV claims nine bytes */
    check(wlfw_parse(bad, sizeof reply, &m) != 0, "parse: a TLV past the message is refused");
    check(wlfw_parse(reply, 6, &m) != 0, "parse: six bytes is not a message");
    /* A result TLV of five bytes: a qmi_response_type is four, so the message is not well formed. */
    static const uint8_t badresult[] = {
        0x02, 0x01, 0x00, 0x20, 0x00, 0x08, 0x00,
        0x02, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    check(wlfw_parse(badresult, sizeof badresult, &m) != 0, "parse: a result TLV of the wrong size is refused");
    /* Truncated at every length: none may be accepted, and none may read past the buffer. */
    for (uint32_t cut = 0; cut < 14; cut++) {
        check(wlfw_parse(reply, cut, &m) != 0, "parse: a truncated message is refused");
    }
    /* A one-byte value TLV at the very end, which must read its value without running past. */
    static const uint8_t edge[] = {0x04, 0x09, 0x00, 0x38, 0x00, 0x04, 0x00, 0x10, 0x01, 0x00, 0x01};
    check(wlfw_parse(edge, sizeof edge, &m) == 0 && m.type == WLFW_INDICATION && m.message == 0x0038,
          "parse: an indication with one one-byte TLV");
}

/* ---- 2249 (W5): the board data download, against bytes worked out by hand ---------------------------------------- */

static void t_bdf_download(void)
{
    /* A four-byte piece, the smallest that shows every field. The layout, from ath11k's qmi_elem_info table for
     * QMI_WLANFW_BDF_DOWNLOAD_REQ_V01 (0x0025):
     *   0x01 valid      u8      1
     *   0x10 file_id    u32     0xff, the id CAP gave (or ath11k's fallback when it gave none)
     *   0x11 total_size u32     bytes still to come, this piece included
     *   0x12 seg_id     u32     from 0
     *   0x13 data       u16 count, then that many bytes
     *   0x14 end        u8      1 on the last piece only
     *   0x15 bdf_type   u8      0 bin, 1 elf, 4 regdb
     * Payload: 4 + 7 + 7 + 7 + (3 + 2 + 4) + 4 + 4 = 42; the message is 7 + 42 = 49. */
    static const uint8_t data[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    static const uint8_t want[] = {
        0x00, 0x05, 0x00, 0x25, 0x00, 0x2a, 0x00,             /* request, transaction 5, id 0x0025, 42 bytes */
        0x01, 0x01, 0x00, 0x01,                               /* valid = 1 */
        0x10, 0x04, 0x00, 0xff, 0x00, 0x00, 0x00,             /* file_id = 0xff */
        0x11, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00,             /* total_size = 4 */
        0x12, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,             /* seg_id = 0 */
        0x13, 0x06, 0x00, 0x04, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, /* data: 4 bytes, count first */
        0x14, 0x01, 0x00, 0x01,                               /* end = 1 */
        0x15, 0x01, 0x00, 0x04,                               /* bdf_type = 4, regdb */
    };
    uint8_t out[128];
    uint32_t n = wlfw_bdf_download(out, sizeof out, 5, 0xffu, 4, 0, data, 4, 1, WLFW_BDF_TYPE_REGDB);
    check(n == 49, "bdf_download: 49 bytes");
    check_bytes("bdf_download: the exact bytes", out, n, want, sizeof want);
    /* The data TLV's length counts the two-byte count as well as the bytes: 2 + 4 = 6. */
    check(wlfw_get16(out + 33) == 6, "bdf_download: the data TLV length counts its count word");
    /* A one-byte buffer too small is refused, not truncated. */
    check(wlfw_bdf_download(out, 48, 5, 0xffu, 4, 0, data, 4, 1, WLFW_BDF_TYPE_REGDB) == 0,
          "bdf_download: a 48-byte buffer is refused");

    /* A piece at the maximum size: 6144 bytes of data, one past what ath11k sends. The message is
     * 7 + 4 + 7 + 7 + 7 + (3 + 2 + 6144) + 4 + 4 = 6189. */
    static uint8_t big[WLFW_MAX_DATA];
    static uint8_t buf[WLFW_MAX_DATA + 64];
    for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)i;
    n = wlfw_bdf_download(buf, sizeof buf, 9, 0x12u, 70000, 7, big, WLFW_MAX_DATA, 0, WLFW_BDF_TYPE_BIN);
    check(n == 6189, "bdf_download: a full 6144-byte piece is 6189 bytes");
    check(wlfw_get16(buf + 5) == 6182, "bdf_download: its payload length is 6182, ath11k's own maximum");
    check(wlfw_get32(buf + 14) == 0x12u, "bdf_download: file_id survives");
    check(wlfw_get32(buf + 21) == 70000u, "bdf_download: total_size is a u32 and keeps its value");
    check(wlfw_get32(buf + 28) == 7u, "bdf_download: seg_id is a u32 and keeps its value");
    /* One byte more than the maximum is refused: WLFW_MAX_DATA is the wire's limit, not a preference. */
    check(wlfw_bdf_download(buf, sizeof buf, 9, 0x12u, 70000, 7, big, WLFW_MAX_DATA + 1, 0, WLFW_BDF_TYPE_BIN) == 0,
          "bdf_download: 6145 bytes of data is refused");
    /* No data at all is still a well-formed request: the count is zero and the TLV is five bytes. */
    n = wlfw_bdf_download(buf, sizeof buf, 3, 1, 0, 0, 0, 0, 1, WLFW_BDF_TYPE_BIN);
    check(n == 45, "bdf_download: a zero-byte piece is 45 bytes");
    check(wlfw_get16(buf + 33) == 2, "bdf_download: a zero-byte piece's data TLV is just its count");
}

static void t_bdf_pieces(void)
{
    /* The pieces a file takes, and the total_size each carries: what is LEFT, this piece included, which is how ath11k
     * sets it. A file of 12000 bytes takes 2 pieces of 6144 and the second says 5856. */
    check(wlfw_bdf_pieces(12000, WLFW_MAX_DATA) == 2, "pieces: 12000 at 6144 is 2");
    check(wlfw_bdf_total(12000, 0) == 12000, "total: the first piece carries the whole file");
    check(wlfw_bdf_total(12000, 1) == 5856, "total: the second carries what is left");
    check(wlfw_bdf_total(12000, 2) == 0, "total: past the end is zero");
    check(wlfw_bdf_pieces(6144, WLFW_MAX_DATA) == 1, "pieces: exactly one piece when the file is one piece");
    check(wlfw_bdf_pieces(6145, WLFW_MAX_DATA) == 2, "pieces: one byte more is two pieces");
    check(wlfw_bdf_pieces(0, WLFW_MAX_DATA) == 1, "pieces: an empty file is still one piece");
    /* The phone's own two files, at the two piece sizes the link can offer. */
    check(wlfw_bdf_pieces(24278, 6144) == 4, "pieces: regdb at 6144 is 4");
    check(wlfw_bdf_pieces(58180, 6144) == 10, "pieces: bdwlan.bin at 6144 is 10");
    check(wlfw_bdf_pieces(24278, 1968) == 13, "pieces: regdb at 1968 is 13");
    check(wlfw_bdf_pieces(58180, 1968) == 30, "pieces: bdwlan.bin at 1968 is 30");
    check(wlfw_bdf_pieces(10, 0) == 1, "pieces: a zero piece size is one piece, not a division by zero");

    /* The type is the file's first bytes, exactly as ath11k tests it (memcmp against ELFMAG). */
    static const uint8_t elf[8] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0};
    static const uint8_t bin[8] = {0x01, 0x00, 0x04, 0x04, 0, 0, 0, 0};
    check(wlfw_bdf_type(elf, sizeof elf) == WLFW_BDF_TYPE_ELF, "type: an ELF file is 1");
    check(wlfw_bdf_type(bin, sizeof bin) == WLFW_BDF_TYPE_BIN, "type: bdwlan.bin's own first bytes are 0");
    check(wlfw_bdf_type(0, 0) == WLFW_BDF_TYPE_BIN, "type: no bytes is BIN, never a read");
    /* The BDF_DOWNLOAD message's name, so the log says what was sent. */
    check(!strcmp(wlfw_name(0x0025), "bdf_download"), "name: 0x0025");
}

/* ---- 2250 (W6): the cold-boot mode request and the request_mem watcher, against bytes worked out by hand -------------- */

static void t_wlan_cfg(void)
{
    /* 2251 (W7): WLAN_CFG, id 0x0023. Payload 406 bytes, whole message 413. The layout, per TLV:
     *   0x10 host_version  "WIN", a top-level QMI_STRING with no count byte: 3 value bytes.
     *   0x11 tgt_cfg       u8 count 9, then 9 * {pipe_num, pipe_dir, nentries, nbytes_max, flags} as le32 = 181 bytes.
     *   0x12 svc_cfg       u8 count 14, then 14 * {service_id, pipe_dir, pipe_num} as le32 = 169 bytes.
     *   0x14 shadow_reg_v2 u8 count 10, then 10 * le32 address = 41 bytes.
     * 3+3 + 3+181 + 3+169 + 3+41 = 406. */
    uint8_t m[512];
    uint32_t n = wlfw_wlan_cfg(m, sizeof m, 0x0023);
    check(n == 413, "wlan_cfg is 413 bytes");
    if (n != 413) return;
    uint8_t head[] = {0x00, 0x23, 0x00, 0x23, 0x00, 0x96, 0x01};   /* type 0, tx 0x23, id 0x0023, len 0x0196 = 406 */
    check_bytes("wlan_cfg header: type 0, transaction, id 0x0023, payload length 406", m, 7, head, sizeof head);
    /* host_version: 0x10, length 3, "WIN". */
    uint8_t version[] = {0x10, 0x03, 0x00, 'W', 'I', 'N'};
    check_bytes("wlan_cfg host_version is TLV 0x10, 3 bytes, \"WIN\"", m + 7, sizeof version, version, sizeof version);
    /* tgt_cfg header: 0x11, length 0x00b5 = 181, count 9, then CE0 {0,2,32,2048,0}. */
    uint8_t tgt[] = {0x11, 0xb5, 0x00, 9,
                     0x00, 0, 0, 0,  0x02, 0, 0, 0,  0x20, 0, 0, 0,  0x00, 0x08, 0, 0,  0x00, 0, 0, 0};
    check_bytes("wlan_cfg tgt_cfg: TLV 0x11, 181 bytes, count 9, CE0 = 0/2/32/2048/0", m + 13, sizeof tgt, tgt, sizeof tgt);
    /* CE4 and CE7 carry CE_ATTR_DIS_INTR, 8; every other pipe's flags are 0. CE7 has no ring at all: 0/0. */
    const uint8_t *p = m + 13 + 4;
    check(get32(p + 20 * 4 + 4) == 2 && get32(p + 20 * 4 + 8) == 256 && get32(p + 20 * 4 + 16) == 8,
          "wlan_cfg CE4 is out, 256 entries of 256 bytes, no interrupt");
    check(get32(p + 20 * 7) == 7 && get32(p + 20 * 7 + 4) == 4 && get32(p + 20 * 7 + 8) == 0 && get32(p + 20 * 7 + 16) == 8,
          "wlan_cfg CE7 is PIPEDIR_INOUT_H2H with no ring and no interrupt");
    /* svc_cfg: 0x12, length 0x00a9 = 169, count 14, then WMI_DATA_VO out on pipe 3. */
    uint8_t svc[] = {0x12, 0xa9, 0x00, 14,
                     0x04, 0x01, 0, 0,  0x02, 0, 0, 0,  0x03, 0, 0, 0};
    check_bytes("wlan_cfg svc_cfg: TLV 0x12, 169 bytes, count 14, VO out on pipe 3", m + 13 + 184, sizeof svc, svc, sizeof svc);
    /* The last service is HTT_DATA_MSG in on pipe 1: service 0x0300, dir 1, pipe 1. */
    const uint8_t *s = m + 13 + 184 + 4;
    check(get32(s + 12 * 13) == 0x300 && get32(s + 12 * 13 + 4) == 1 && get32(s + 12 * 13 + 8) == 1,
          "wlan_cfg the last service is HTT_DATA_MSG in on pipe 1");
    /* shadow_reg_v2: 0x14, length 0x0029 = 41, count 10, then CE0's source-ring HP at 0x01b80400. */
    uint8_t shadow[] = {0x14, 0x29, 0x00, 10, 0x00, 0x04, 0xb8, 0x01};
    check_bytes("wlan_cfg shadow_reg_v2: TLV 0x14, 41 bytes, count 10, CE0 src HP", m + 13 + 184 + 172, sizeof shadow, shadow, sizeof shadow);

    /* The shadow list on its own, against hand-worked values. CE blocks are 0x2000 apart from CE0_SRC 0x01b80000 and
     * CE0_DST 0x01b81000; a source ring's HP is +0x400, a destination ring's TP is +0x400 and its status ring's TP is
     * +0x408 + 4 (ath11k's HAL_OFFSET_FROM_HP_TO_TP, because that ring is a destination ring in its config table). */
    uint32_t list[36];
    uint32_t k = wlfw_shadow_config(list, 36);
    check(k == 10, "the shadow list has 10 words, as four source and three destination pipes imply");
    static const uint32_t want[10] = {0x01b80400u, 0x01b83400u, 0x01b8340cu, 0x01b85400u, 0x01b8540cu,
                                      0x01b86400u, 0x01b88400u, 0x01b8b400u, 0x01b8b40cu, 0x01b8e400u};
    for (uint32_t i = 0; i < k && i < 10; ++i) check(list[i] == want[i], "shadow word is the one ath11k's loop would record");
    /* A capacity that cannot hold the whole list is refused outright, never truncated: a short list is a different list
     * to the firmware, and 9 of 10 words would leave one ring unshadowed. */
    uint32_t small[9];
    check(wlfw_shadow_config(small, 9) == 0, "a shadow list that does not fit is refused, not truncated");
    /* Capacity exactly the list's length is enough. */
    uint32_t exact[10];
    check(wlfw_shadow_config(exact, 10) == 10, "a shadow list of exactly its own length fits");
    /* A buffer too small for the whole message is refused rather than written short. 412 is one byte short of 413. */
    uint8_t tight[412];
    check(wlfw_wlan_cfg(tight, sizeof tight, 1) == 0, "wlan_cfg in a 412-byte buffer is refused");
    check(wlfw_wlan_cfg(m, sizeof m, 1) == 413, "wlan_cfg in a 413-byte buffer fits");

    /* No 0x13 TLV: ath11k sets shadow_reg_valid = 0 and uses only the v2 list. A 0x13 in the message would be the older
     * {u16 id, u16 offset} form, which this chip's driver never sends. */
    int saw13 = 0;
    for (uint32_t at = 7; at < n;) {
        const uint32_t len = get16(m + at + 1);
        if (m[at] == 0x13) saw13 = 1;
        at += 3 + len;
    }
    check(!saw13, "wlan_cfg carries no 0x13 (the older shadow_reg form)");
}

static void t_wlan_ini(void)
{
    /* 2251 (W7): WLAN_INI, id 0x002F. One TLV, 0x10 u8 = 1 (log on), 4-byte payload, 11-byte message. */
    uint8_t m[64];
    uint32_t n = wlfw_wlan_ini(m, sizeof m, 0x0007, 1);
    check(n == 11, "wlan_ini is 11 bytes");
    uint8_t want[] = {0x00, 0x07, 0x00, 0x2f, 0x00, 0x04, 0x00, 0x10, 0x01, 0x00, 0x01};
    check_bytes("wlan_ini: type 0, transaction 7, id 0x002F, TLV 0x10 = 1", m, n, want, sizeof want);
    /* Log off is the same message with a zero, which ath11k sends on the stop path; not used by this stage, but the
     * encoder must not be silently fixed at 1. */
    n = wlfw_wlan_ini(m, sizeof m, 1, 0);
    check(n == 11 && m[10] == 0, "wlan_ini can encode enablefwlog = 0");
    /* A buffer that cannot hold it is refused. */
    uint8_t tight[10];
    check(wlfw_wlan_ini(tight, sizeof tight, 1, 1) == 0, "wlan_ini in a 10-byte buffer is refused");
}

static void t_mission_modes(void)
{
    /* 2251 (W7): the three modes on the same message id. The mode number is what tells them apart on the wire, so the
     * encoder is pinned for all three rather than only cold boot. */
    uint8_t m[32];
    struct {uint32_t mode; const char *what;} modes[] = {
        {WLFW_MODE_COLD_BOOT, "cold boot"}, {WLFW_MODE_NORMAL, "mission"}, {WLFW_MODE_OFF, "off"},
    };
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t n = wlfw_wlan_mode(m, sizeof m, 9, modes[i].mode);
        check(n == 18, "wlan_mode is 18 bytes");
        check(m[0] == 0 && m[1] == 9 && m[2] == 0 && m[3] == 0x22 && m[4] == 0 && m[5] == 11 && m[6] == 0,
              "wlan_mode header is type 0, the transaction, id 0x0022 and an 11-byte payload");
        check(m[7] == 0x01 && m[8] == 4 && m[9] == 0 && get32(m + 10) == modes[i].mode,
              "wlan_mode TLV 0x01 carries the mode as a le32");
        check(m[14] == 0x10 && m[15] == 1 && m[16] == 0 && m[17] == 0,
              "wlan_mode TLV 0x10 carries hw_debug = 0 and its presence is the valid bit");
    }
}

static void t_wlan_mode(void)
{
    /* WLAN_MODE request, id 0x0022, from ath11k_qmi_wlanfw_mode_send with mode 7 (ATH11K_FIRMWARE_MODE_COLD_BOOT):
     * hw_debug_valid = 1 and hw_debug = 0, so TLV 0x10 is present and holds one zero byte.
     * Payload: (3 + 4) for the mode and (3 + 1) for hw_debug = 11, ath11k's own
     * QMI_WLANFW_WLAN_MODE_REQ_MSG_V01_MAX_LEN; the message is 7 + 11 = 18. */
    static const uint8_t want[] = {
        0x00, 0x05, 0x00, 0x22, 0x00, 0x0b, 0x00,             /* request, transaction 5, id 0x0022, 11 bytes */
        0x01, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,             /* mode = 7, cold boot */
        0x10, 0x01, 0x00, 0x00,                               /* hw_debug = 0, present because valid is set */
    };
    uint8_t out[64];
    const uint32_t n = wlfw_wlan_mode(out, sizeof out, 5, WLFW_MODE_COLD_BOOT);
    check(n == 18, "wlan_mode: 18 bytes");
    check_bytes("wlan_mode: the exact bytes for mode 7", out, n, want, sizeof want);

    /* The same message with mission mode's number, so the mode is the field and not a constant in the header. */
    static const uint8_t normal[] = {
        0x00, 0x06, 0x00, 0x22, 0x00, 0x0b, 0x00,
        0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x01, 0x00, 0x00,
    };
    const uint32_t m = wlfw_wlan_mode(out, sizeof out, 6, WLFW_MODE_NORMAL);
    check(m == 18, "wlan_mode: mode 0 is still 18 bytes");
    check_bytes("wlan_mode: the mode field is mode 0 for mission", out, m, normal, sizeof normal);

    /* Every mode number is one u32 and keeps its value; the enum's own values are the ones ath11k's core.h has. */
    static const uint32_t modes[] = {WLFW_MODE_NORMAL, WLFW_MODE_OFF, WLFW_MODE_COLD_BOOT};
    static const uint32_t values[] = {0, 4, 7};
    for (unsigned i = 0; i < 3; i++) {
        check(modes[i] == values[i], "wlan_mode: the mode enum's own numbers");
        check(wlfw_wlan_mode(out, sizeof out, 1, modes[i]) == 18, "wlan_mode: 18 bytes for every mode");
        check(wlfw_get32(out + 10) == modes[i], "wlan_mode: the mode survives the encoding");
    }
    /* A buffer that cannot hold it is refused, not truncated: 17 bytes is one short. */
    check(wlfw_wlan_mode(out, 17, 5, WLFW_MODE_COLD_BOOT) == 0, "wlan_mode: a 17-byte buffer is refused");
    check(wlfw_wlan_mode(0, sizeof out, 5, WLFW_MODE_COLD_BOOT) == 0, "wlan_mode: no buffer is refused");
    check(!strcmp(wlfw_name(0x0022), "wlan_mode"), "name: 0x0022");
}

static void t_request_mem(void)
{
    /* A request_mem indication (0x0035) asking for one 1 MiB M3 segment with no configs. TLV 0x01: u8 count = 1, then
     * {u32 size, u32 type, u8 mem_cfg_len} = 10 bytes, so the payload is 3 + 10 = 13 and the message is 20.
     * This is a message ath11k never registers for on this chip; the decoder exists so the log can say what a firmware
     * that sends it anyway wanted. */
    static const uint8_t ask[] = {
        0x04, 0x07, 0x00, 0x35, 0x00, 0x0d, 0x00,
        0x01, 0x0a, 0x00, 0x01,                                     /* one segment */
        0x00, 0x00, 0x10, 0x00,                                     /* size = 0x100000 */
        0x03, 0x00, 0x00, 0x00,                                     /* type = 3, M3 */
        0x00,                                                       /* mem_cfg_len = 0 */
    };
    WlfwMessage m;
    check(wlfw_parse(ask, sizeof ask, &m) == 0 && m.type == WLFW_INDICATION && m.message == 0x0035,
          "request_mem: parses as a request_mem indication");
    uint32_t count = 0;
    WlfwMemSegment seg = {0, 0, 0};
    check(wlfw_request_mem(ask, sizeof ask, &count, &seg) == 0, "request_mem: one segment decoded");
    check(count == 1 && seg.size == 0x100000u && seg.type == WLFW_MEM_M3 && seg.configs == 0,
          "request_mem: size 1 MiB, type M3, no configs");
    check(!strcmp(wlfw_mem_type_name(seg.type), "m3"), "request_mem: the type names itself");

    /* Every type the enum has, named: MSA 0, DDR 1, BDF 2, M3 3, CAL 4, DPD 5, and anything else unknown. */
    static const char *const names[] = {"msa", "ddr", "bdf", "m3", "cal", "dpd"};
    for (uint32_t i = 0; i < 6; i++) check(!strcmp(wlfw_mem_type_name(i), names[i]), "request_mem: the type name");
    check(!strcmp(wlfw_mem_type_name(99), "unknown"), "request_mem: an unknown type says so");

    /* A count of zero is "no segment", reported as such rather than as a malformed message. */
    static const uint8_t none[] = {0x04, 0x07, 0x00, 0x35, 0x00, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00};
    check(wlfw_request_mem(none, sizeof none, &count, &seg) == 1, "request_mem: a zero count is not a segment");

    /* Truncated: the count says one but the segment is not all there. Refused, never read past. */
    static const uint8_t cut[] = {0x04, 0x07, 0x00, 0x35, 0x00, 0x08, 0x00, 0x01, 0x05, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00};
    check(wlfw_parse(cut, sizeof cut, &m) == 0, "request_mem: the truncated message still parses as a TLV chain");
    check(wlfw_request_mem(cut, sizeof cut, &count, &seg) == -1, "request_mem: a short segment is malformed");
    /* Every truncation of the well-formed message is refused rather than read past. */
    for (uint32_t at = 7; at < sizeof ask; at++) check(wlfw_request_mem(ask, at, &count, &seg) != 0,
                                                       "request_mem: a truncated TLV is never decoded");
    /* No TLV at all: not this message's shape, reported as absent rather than malformed. */
    static const uint8_t bare[] = {0x04, 0x07, 0x00, 0x35, 0x00, 0x00, 0x00};
    check(wlfw_request_mem(bare, sizeof bare, &count, &seg) == 1, "request_mem: no TLV is absent, not malformed");
}

/* ---- the names the log uses ------------------------------------------------------------------------ */
static void t_names(void)
{
    check(!strcmp(wlfw_name(0x0020), "ind_register"), "name: 0x0020");
    check(!strcmp(wlfw_name(0x0034), "host_cap"), "name: 0x0034");
    check(!strcmp(wlfw_name(0x0024), "cap"), "name: 0x0024");
    check(!strcmp(wlfw_name(0x004c), "device_info"), "name: 0x004C");
    check(!strcmp(wlfw_name(0x9999), "unknown"), "name: an unknown id names itself unknown");
    check(!strcmp(wlfw_indication_name(0x0038), "fw_init_done"), "name: fw_init_done indication");
    check(!strcmp(wlfw_indication_name(0x0021), "fw_ready"), "name: fw_ready indication");
    /* The service numbers the phone printed: 0x45 at instance 0x301. */
    check(WLFW_SERVICE == 0x45u && WLFW_ANNOUNCED == 0x301u, "service: 0x45 announced as 0x301");
}

int main(void)
{
    t_ind_register();
    t_host_cap();
    t_empty();
    t_parse();
    t_bdf_download();
    t_bdf_pieces();
    t_wlan_mode();
    t_wlan_cfg();
    t_wlan_ini();
    t_mission_modes();
    t_request_mem();
    t_names();
    if (failures) {printf("FAIL: %d of %d checks\n", failures, checks); return 1;}
    printf("PASS: WLFW QMI: %d checks, every encoded byte and every decoded field as the documented layout says\n", checks);
    return 0;
}
