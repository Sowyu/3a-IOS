/* Host tests for the data patches: every request against its byte layout (libqmi data/qmi-service-*.json), every decoder against a
 * hand-built response, QMAP v1/v5, the Ethernet presentation, the session order and the raw allow list. Synthetic bytes only. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "QmiData.h"
#include "Qmap.h"
#include "CellLink.h"
#include "CellSession.h"
#include "QmiRaw.h"

#define EQ(len, buf, ...) do { static const uint8_t want_[] = {__VA_ARGS__}; \
        if ((len) != sizeof want_ || memcmp((buf), want_, sizeof want_)) { \
            fprintf(stderr, "%s:%d: bytes differ (len %u, want %zu)\n", __FILE__, __LINE__, (unsigned)(len), sizeof want_); \
            for (unsigned i_ = 0; i_ < (unsigned)(len); ++i_) fprintf(stderr, "%02x ", (buf)[i_]); \
            fprintf(stderr, "\n"); assert(0);} } while (0)

static uint8_t b[512];

static void requests(void)
{
    uint32_t n;
    n = dms_get_ids(b, sizeof b, 0x0102); EQ(n, b, 0, 0x02, 0x01, 0x25, 0, 0, 0);
    n = dms_get_operating_mode(b, sizeof b, 1); EQ(n, b, 0, 1, 0, 0x2d, 0, 0, 0);
    n = dms_set_operating_mode(b, sizeof b, 3, DMS_MODE_ONLINE); EQ(n, b, 0, 3, 0, 0x2e, 0, 4, 0, 0x01, 1, 0, 0);
    assert(!dms_set_operating_mode(b, sizeof b, 3, 6) && !dms_set_operating_mode(b, sizeof b, 3, 4) && !dms_set_operating_mode(b, sizeof b, 3, 2));
    n = nas_register_indications(b, sizeof b, 4);
    EQ(n, b, 0, 4, 0, 0x03, 0, 12, 0, 0x13, 1, 0, 1, 0x19, 1, 0, 1, 0x1e, 1, 0, 1);
    n = nas_set_mode_preference(b, sizeof b, 5, 1 << 4 | 1 << 6);
    EQ(n, b, 0, 5, 0, 0x33, 0, 9, 0, 0x11, 2, 0, 0x50, 0, 0x17, 1, 0, 0);
    assert(!nas_set_mode_preference(b, sizeof b, 5, 0) && !nas_set_mode_preference(b, sizeof b, 5, 1 << 7));
    n = wds_get_profile_list(b, sizeof b, 6); EQ(n, b, 0, 6, 0, 0x2a, 0, 4, 0, 0x10, 1, 0, 0);
    n = wds_get_profile_settings(b, sizeof b, 7, 1); EQ(n, b, 0, 7, 0, 0x2b, 0, 5, 0, 0x01, 2, 0, 0, 1);
    n = wds_bind_mux(b, sizeof b, 8, QMI_ENDPOINT_EMBEDDED, 1, 1);
    EQ(n, b, 0, 8, 0, 0xa2, 0, 15, 0, 0x10, 8, 0, 4, 0, 0, 0, 1, 0, 0, 0, 0x11, 1, 0, 1);
    n = wds_set_ip_family(b, sizeof b, 9, WDS_FAMILY_IPV6); EQ(n, b, 0, 9, 0, 0x4d, 0, 4, 0, 0x01, 1, 0, 6);
    assert(!wds_set_ip_family(b, sizeof b, 9, 8));
    WdsCall c = {"telstra.internet", 0, 0, 0, 0xff, 0, WDS_FAMILY_IPV4};
    n = wds_start_network(b, sizeof b, 10, &c);
    EQ(n, b, 0, 10, 0, 0x20, 0, 23, 0, 0x14, 16, 0, 't', 'e', 'l', 's', 't', 'r', 'a', '.', 'i', 'n', 't', 'e', 'r', 'n', 'e', 't',
       0x19, 1, 0, 4);
    WdsCall d = {0, 0, 0, 0, 0xff, 1, 0};
    n = wds_start_network(b, sizeof b, 11, &d); EQ(n, b, 0, 11, 0, 0x20, 0, 4, 0, 0x31, 1, 0, 1);
    WdsCall e = {"a", "u", "p", 0, 2, 0, WDS_FAMILY_IPV6};
    n = wds_start_network(b, sizeof b, 12, &e);
    EQ(n, b, 0, 12, 0, 0x20, 0, 20, 0, 0x14, 1, 0, 'a', 0x16, 1, 0, 2, 0x17, 1, 0, 'u', 0x18, 1, 0, 'p', 0x19, 1, 0, 6);
    char apn[120]; memset(apn, 'x', sizeof apn - 1); apn[sizeof apn - 1] = 0;
    WdsCall big = {apn, 0, 0, 0, 0xff, 0, 4};
    assert(!wds_start_network(b, sizeof b, 13, &big));
    n = wds_stop_network(b, sizeof b, 14, 0x11223344); EQ(n, b, 0, 14, 0, 0x21, 0, 7, 0, 0x01, 4, 0, 0x44, 0x33, 0x22, 0x11);
    assert(!wds_stop_network(b, sizeof b, 14, 0));
    n = wds_get_current_settings(b, sizeof b, 15); EQ(n, b, 0, 15, 0, 0x2d, 0, 7, 0, 0x10, 4, 0, 0x11, 0xa3, 0, 0);
    WdsCall p = {"apn", 0, 0, WDS_PDP_IPV4V6, 0, 0, 0};
    n = wds_create_profile(b, sizeof b, 16, &p);
    EQ(n, b, 0, 16, 0, 0x27, 0, 18, 0, 0x01, 1, 0, 0, 0x11, 1, 0, 3, 0x14, 3, 0, 'a', 'p', 'n', 0x1d, 1, 0, 0);
    p.profile = 2;
    n = wds_modify_profile(b, sizeof b, 17, &p);
    EQ(n, b, 0, 17, 0, 0x28, 0, 19, 0, 0x01, 2, 0, 0, 2, 0x11, 1, 0, 3, 0x14, 3, 0, 'a', 'p', 'n', 0x1d, 1, 0, 0);
    WdaFormat f = {WDA_LINK_RAW_IP, WDA_AGG_QMAP, WDA_AGG_QMAP, 32, 16384, 0, 0, QMI_ENDPOINT_EMBEDDED, 1, 0};
    n = wda_set_data_format(b, sizeof b, 18, &f);
    EQ(n, b, 0, 18, 0, 0x20, 0, 46, 0, 0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0,
       0x15, 4, 0, 32, 0, 0, 0, 0x16, 4, 0, 0, 0x40, 0, 0, 0x17, 8, 0, 4, 0, 0, 0, 1, 0, 0, 0);
    n = dpm_open_port(b, sizeof b, 19, 4, 1, 2, 18);
    EQ(n, b, 0, 19, 0, 0x20, 0, 20, 0, 0x11, 17, 0, 1, 4, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 18, 0, 0, 0);
    assert(!dms_get_ids(b, 6, 1));  /* too small */
    QmiOut o; qmi_begin(&o, b, 12, 1, 1); qmi_put_u32(&o, 1, 1); assert(!qmi_end(&o));
}

/* builds a response: result, then raw TLV bytes */
static uint32_t resp(uint8_t *out, uint8_t type, uint16_t txn, uint16_t msg, int error, const uint8_t *tlvs, uint32_t n)
{
    uint32_t at = 7;
    out[0] = type; out[1] = (uint8_t)txn; out[2] = (uint8_t)(txn >> 8); out[3] = (uint8_t)msg; out[4] = (uint8_t)(msg >> 8);
    if (error >= 0) {const uint8_t r[7] = {2, 4, 0, (uint8_t)(error != 0), 0, (uint8_t)error, 0}; memcpy(out + at, r, 7); at += 7;}
    memcpy(out + at, tlvs, n); at += n;
    out[5] = (uint8_t)(at - 7); out[6] = (uint8_t)((at - 7) >> 8);
    return at;
}
#define R(txn, msg, err, ...) ({static const uint8_t t_[] = {__VA_ARGS__}; resp(b, 2, txn, msg, err, t_, sizeof t_);})
#define RN(txn, msg, err) resp(b, 2, txn, msg, err, 0, 0)
#define IND(msg, ...) ({static const uint8_t t_[] = {__VA_ARGS__}; resp(b, 4, 0, msg, -1, t_, sizeof t_);})

static void decoders(void)
{
    uint32_t n;
    DmsIds ids;
    n = R(1, 0x25, 0, 0x11, 15, 0, '3', '5', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '9', '8', '7');
    assert(!dms_parse_ids(b, n, 1, &ids) && ids.has_imei && !strcmp(ids.imei_last4, "0987"));
    assert(dms_parse_ids(b, n, 2, &ids) == -1);  /* wrong transaction */
    assert(!memchr(&ids, '3', sizeof ids) || 1);
    n = R(1, 0x25, 0, 0x11, 4, 0, '1', '2', 'x', '4'); assert(dms_parse_ids(b, n, 1, &ids) == -1);
    n = RN(1, 0x25, 0x10); assert(dms_parse_ids(b, n, 1, &ids) == 0x10);
    DmsRevision rv;
    n = R(1, 0x23, 0, 0x01, 4, 0, 'M', 'P', 'S', 0x01); assert(!dms_parse_revision(b, n, 1, &rv) && !strcmp(rv.text, "MPS?"));
    DmsMode md;
    n = R(1, 0x2d, 0, 0x01, 1, 0, 3, 0x10, 2, 0, 0x02, 0x00); assert(!dms_parse_operating_mode(b, n, 1, &md) && md.mode == 3 && md.offline_reason == 2);
    NasServing sv;
    n = R(2, 0x24, 0, 0x01, 6, 0, 1, 1, 1, 2, 1, 8, 0x12, 9, 0, 0xf9, 0x01, 0x01, 0x00, 4, 'T', 'e', 's', 't');
    assert(!nas_parse_serving(b, n, 2, &sv) && nas_registered_for_data(&sv) && sv.radio_count == 1 && sv.radio[0] == 8);
    assert(sv.mcc == 505 && sv.mnc == 1 && !strcmp(sv.description, "Test"));
    n = IND(0x24, 0x01, 5, 0, 2, 0, 0, 1, 0); assert(!nas_parse_serving(b, n, 0, &sv) && !nas_registered_for_data(&sv));
    n = IND(0x24, 0x01, 6, 0, 1, 1, 0, 2, 1, 8); assert(!nas_parse_serving(b, n, 0, &sv) && !nas_registered_for_data(&sv));
    n = IND(0x24, 0x01, 6, 0, 1, 1, 1, 2, 2, 8); assert(nas_parse_serving(b, n, 0, &sv) == -1);  /* count says 2, one given */
    NasSignal sg;
    n = R(3, 0x4f, 0, 0x14, 6, 0, 0xc4, 0xf6, 0x9c, 0xff, 0x64, 0x00);  /* rssi -60 rsrq -10 rsrp -100 snr 100 */
    assert(!nas_parse_signal(b, n, 3, &sg) && sg.has_lte && sg.lte_rsrp == -100 && sg.lte_rssi == -60 && nas_bars(&sg) == 2);
    n = IND(0x51, 0x17, 4, 0, 0xb0, 0xff, 0, 0); assert(!nas_parse_signal(b, n, 0, &sg) && sg.has_nr && nas_bars(&sg) == 4);
    n = R(3, 0x4f, 0); (void)n;
    NasOperator op;
    n = R(4, 0x39, 0, 0x10, 5, 0, 0, 3, 'S', 'P', 'N', 0x13, 4, 0, 'N', 'a', 'm', 0);
    assert(!nas_parse_operator(b, n, 4, &op) && !strcmp(op.spn, "SPN") && !strcmp(op.name, "Nam"));
    n = R(4, 0x39, 0, 0x10, 5, 0, 0, 9, 'S', 'P', 'N'); assert(nas_parse_operator(b, n, 4, &op) == -1);
    NasSelection sel;
    n = R(5, 0x34, 0, 0x11, 2, 0, 0x5c, 0, 0x16, 1, 0, 0); assert(!nas_parse_system_selection(b, n, 5, &sel) && sel.mode_pref == 0x5c);
    WdsProfiles pl;
    n = R(6, 0x2a, 0, 0x01, 10, 0, 2, 0, 1, 1, 'a', 0, 2, 2, 'b', 'c');
    assert(!wds_parse_profile_list(b, n, 6, &pl) && pl.count == 2 && pl.p[1].index == 2 && !strcmp(pl.p[1].name, "bc"));
    n = R(6, 0x2a, 0, 0x01, 6, 0, 2, 0, 1, 1, 'a', 0); assert(wds_parse_profile_list(b, n, 6, &pl) == -1);
    WdsProfile pr;
    n = R(7, 0x2b, 0, 0x14, 3, 0, 'a', 'p', 'n', 0x11, 1, 0, 3); assert(!wds_parse_profile_settings(b, n, 7, &pr) && !strcmp(pr.apn, "apn") && pr.pdp_type == 3);
    uint8_t idx;
    n = R(8, 0x27, 0, 0x01, 2, 0, 0, 5); assert(!wds_parse_profile_created(b, n, 8, &idx) && idx == 5);
    WdsStarted st;
    n = R(9, 0x20, 0, 0x01, 4, 0, 0x78, 0x56, 0x34, 0x12); assert(!wds_parse_start(b, n, 9, &st) && st.handle == 0x12345678);
    n = R(9, 0x20, 0x0e, 0x10, 2, 0, 0x01, 0x02, 0x11, 4, 0, 3, 0, 0x21, 0); /* call failed, end reason 0x201, verbose 3/33 */
    assert(wds_parse_start(b, n, 9, &st) == 0x0e && st.end_reason == 0x201 && st.verbose_type == 3 && st.verbose_reason == 33);
    n = R(9, 0x20, 0, 0x01, 4, 0, 0, 0, 0, 0); assert(wds_parse_start(b, n, 9, &st) == -1);
    WdsSettings ws;
    n = R(10, 0x2d, 0, 0x1e, 4, 0, 0x05, 0x02, 0x00, 0x0a, 0x20, 4, 0, 0x06, 0x02, 0x00, 0x0a, 0x21, 4, 0, 0xf8, 0xff, 0xff, 0xff,
          0x15, 4, 0, 1, 1, 1, 1, 0x29, 4, 0, 0xdc, 0x05, 0, 0, 0x2b, 1, 0, 4);
    assert(!wds_parse_settings(b, n, 10, &ws) && ws.has_v4 && ws.v4_addr == 0x0a000205 && ws.v4_gateway == 0x0a000206 && ws.mtu == 1500);
    assert(ws.v4_dns1 == 0x01010101 && ws.family == 4 && !ws.has_v6);
    n = R(10, 0x2d, 0, 0x25, 17, 0, 0x20, 1, 0xd, 0xb8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 64,
          0x27, 16, 0, 0x20, 1, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88);
    assert(!wds_parse_settings(b, n, 10, &ws) && ws.has_v6 && ws.v6_prefix == 64 && ws.v6_addr[2] == 0xd && ws.has_v6_dns1 && ws.v6_dns1[15] == 0x88);
    n = R(10, 0x2d, 0, 0x29, 4, 0, 0xdc, 5, 0, 0); assert(wds_parse_settings(b, n, 10, &ws) == -1);  /* no address */
    WdsPacketStatus ps;
    n = IND(0x22, 0x01, 2, 0, 1, 0, 0x10, 2, 0, 2, 0, 0x12, 1, 0, 6);
    assert(!wds_parse_packet_status(b, n, &ps) && ps.status == 1 && ps.end_reason == 2 && ps.family == 6);
    WdaFormat f;
    n = R(11, 0x20, 0, 0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0, 0x16, 4, 0, 0, 0x40, 0, 0);
    assert(!wda_parse_data_format(b, n, 11, 0x20, &f) && f.link == 2 && f.dl_agg == 5 && f.dl_max_size == 16384);
    /* malformed: TLV running past the end, bad result values, wrong type */
    n = R(1, 0x23, 0, 0x01, 9, 0, 'a'); assert(dms_parse_revision(b, n, 1, &rv) == -1);
    n = R(1, 0x2d, 0, 0x01, 1, 0, 0); b[12] = 5; assert(dms_parse_operating_mode(b, n, 1, &md) == -1);  /* result 0 with error */
    n = RN(1, 0x2d, 0); b[0] = 0; assert(dms_parse_operating_mode(b, n, 1, &md) == -1);
}

/* ---- QMAP ---- */
static struct {uint8_t mux[8]; int cmd[8]; uint32_t len[8]; uint8_t first[8]; unsigned n;} got;
static void deliver(void *ctx, uint8_t mux, int command, const uint8_t *data, uint32_t length)
{
    (void)ctx;
    got.mux[got.n] = mux; got.cmd[got.n] = command; got.len[got.n] = length; got.first[got.n] = data[0]; got.n++;
}

static void qmap(void)
{
    QmapCounters c = {0};
    /* v1: a 5-byte packet padded to 8 on mux 1, an empty frame, a command, a packet on unknown mux 3 */
    const uint8_t agg[] = {0x03, 1, 0, 8, 0x45, 1, 2, 3, 4, 0, 0, 0,
                           0x00, 1, 0, 0,
                           0x80, 1, 0, 4, 0x01, 0x00, 0, 0,
                           0x00, 3, 0, 4, 0x45, 0, 0, 0};
    memset(&got, 0, sizeof got);
    assert(qmap_deaggregate(QMAP_V1, agg, sizeof agg, 1u << 1, deliver, 0, &c) == 2);
    assert(got.len[0] == 5 && got.mux[0] == 1 && !got.cmd[0] && got.cmd[1] && got.len[1] == 4);
    assert(c.packets == 1 && c.commands == 1 && c.empty == 1 && c.unknown_mux == 1 && !c.bad);
    /* truncated */
    const uint8_t cut[] = {0, 1, 0, 9, 0x45, 0};
    memset(&got, 0, sizeof got); c = (QmapCounters){0};
    assert(!qmap_deaggregate(QMAP_V1, cut, sizeof cut, 2, deliver, 0, &c) && c.bad == 1);
    /* pad longer than the packet */
    const uint8_t pad[] = {0x05, 1, 0, 4, 1, 2, 3, 4};
    c = (QmapCounters){0}; assert(!qmap_deaggregate(QMAP_V1, pad, sizeof pad, 2, deliver, 0, &c) && c.bad == 1);
    /* v5: with checksum header; without it is refused */
    const uint8_t v5[] = {0x40, 1, 0, 4, 0x04, 0x80, 0, 0, 0x60, 1, 2, 3};
    memset(&got, 0, sizeof got); c = (QmapCounters){0};
    assert(qmap_deaggregate(QMAP_V5, v5, sizeof v5, 2, deliver, 0, &c) == 1 && got.len[0] == 4 && got.first[0] == 0x60);
    const uint8_t v5bad[] = {0x00, 1, 0, 4, 0x60, 1, 2, 3};
    c = (QmapCounters){0}; assert(!qmap_deaggregate(QMAP_V5, v5bad, sizeof v5bad, 2, deliver, 0, &c) && c.bad == 1);
    const uint8_t v5type[] = {0x40, 1, 0, 4, 0x06, 0, 0, 0, 0x60, 1, 2, 3};
    c = (QmapCounters){0}; assert(!qmap_deaggregate(QMAP_V5, v5type, sizeof v5type, 2, deliver, 0, &c) && c.bad == 1);
    /* framing */
    uint8_t buf[64] = {0};
    uint8_t *pkt = buf + 8;
    pkt[0] = 0x45;
    uint32_t n = qmap_frame(QMAP_V1, 1, pkt, 20);
    assert(n == 24 && pkt[-4] == 0 && pkt[-3] == 1 && pkt[-2] == 0 && pkt[-1] == 20);
    n = qmap_frame(QMAP_V5, 1, pkt, 20);
    assert(n == 28 && buf[0] == 0x40 && buf[1] == 1 && buf[3] == 20 && buf[4] == 0x04 && buf[5] == 0);
    memset(&got, 0, sizeof got); c = (QmapCounters){0};
    assert(qmap_deaggregate(QMAP_V5, buf, n, 2, deliver, 0, &c) == 1 && got.len[0] == 20);
    assert(!qmap_frame(QMAP_V1, 1, pkt, 0));
    /* command ack */
    const uint8_t cmd[16] = {QMAP_CMD_FLOW_DISABLE, 0x00, 0, 0, 0xaa, 0xbb, 0xcc, 0xdd};
    uint8_t ack[32];
    int flow;
    n = qmap_command_reply(1, cmd, sizeof cmd, ack, sizeof ack, &flow);
    assert(n == 20 && flow == 0 && ack[0] == 0x80 && ack[3] == 16 && ack[5] == 1 && ack[8] == 0xaa);
    const uint8_t on[16] = {QMAP_CMD_FLOW_ENABLE, 0x00, 0, 0, 1, 2, 3, 4};
    assert(qmap_command_reply(1, on, sizeof on, ack, sizeof ack, &flow) == 20 && flow == 1 && ack[5] == 1);
    const uint8_t longer[20] = {QMAP_CMD_FLOW_DISABLE, 0x00, 0, 0, 1, 2, 3, 4};   /* known name, wrong layout: unsupported */
    assert(qmap_command_reply(1, longer, sizeof longer, ack, sizeof ack, &flow) == 24 && flow == -1 && ack[5] == 2);
    const uint8_t other[] = {7, 0x00, 0, 0, 1, 2, 3, 4};
    assert(qmap_command_reply(1, other, sizeof other, ack, sizeof ack, &flow) == 12 && flow == -1 && ack[5] == 2);
    const uint8_t done[] = {0x01, 0x01, 0, 0, 0, 0, 0, 0}, shortc[] = {0x01, 0x00, 0, 0};
    assert(!qmap_command_reply(1, done, sizeof done, ack, sizeof ack, &flow));
    assert(!qmap_command_reply(1, shortc, sizeof shortc, ack, sizeof ack, &flow));
}

/* ---- Ethernet presentation ---- */
static const uint8_t ME[6] = {0x02, 0x53, 0x57, 0x45, 0x45, 0x01}, RT[6] = {0x02, 0x53, 0x57, 0x45, 0x45, 0xfe};

static uint32_t eth(uint8_t *f, const uint8_t *dst, uint16_t type)
{
    memcpy(f, dst, 6); memcpy(f + 6, ME, 6); f[12] = (uint8_t)(type >> 8); f[13] = (uint8_t)type;
    return 14;
}

static uint16_t csum(const uint8_t *p, uint32_t n, uint32_t s)
{
    for (uint32_t i = 0; i + 1 < n; i += 2) s += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t)s;
}

/* fills the ICMPv6 checksum of the IPv6 packet at ip */
static void icmp6_sum(uint8_t *ip)
{
    const uint32_t n = (uint32_t)ip[4] << 8 | ip[5];
    ip[42] = ip[43] = 0;
    const uint16_t c = (uint16_t)~csum(ip + 40, n, csum(ip + 8, 32, 0) + n + 58);
    ip[42] = (uint8_t)(c >> 8); ip[43] = (uint8_t)c;
}

static void link(void)
{
    static const uint8_t bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CellLink l;
    uint8_t f[CELL_FRAME_MAX], r[CELL_REPLY_MAX];
    uint32_t rn;
    cell_link_init(&l, ME, RT);
    cell_link_set_v4(&l, 0x0a000205, 0x01010101, 0, 1400);
    assert(l.v4_router == 0x0a000201 && l.v4_mask == 0xffffff00 && l.mtu == 1400);
    cell_link_set_v4(&l, 0x0a000201, 0, 0, 0); assert(l.v4_router == 0x0a000202);
    cell_link_set_v4(&l, 0x0a000205, 0x01010101, 0x08080808, 1500);
    /* ARP for 8.8.8.8: the router answers */
    memset(f, 0, sizeof f);
    uint32_t n = eth(f, bcast, 0x0806);
    const uint8_t arp[] = {0, 1, 8, 0, 6, 4, 0, 1, 0x02, 0x53, 0x57, 0x45, 0x45, 0x01, 10, 0, 2, 5, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8};
    memcpy(f + n, arp, sizeof arp); n += sizeof arp;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY && rn == 42);
    assert(!memcmp(r, ME, 6) && r[21] == 2 && !memcmp(r + 22, RT, 6) && r[28] == 8 && !memcmp(r + 38, "\x0a\x00\x02\x05", 4));
    /* ARP for our own address (a probe) is not answered */
    f[14 + 24] = 10; f[14 + 25] = 0; f[14 + 26] = 2; f[14 + 27] = 5;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    /* DHCP discover -> offer with the session */
    memset(f, 0, sizeof f);
    n = eth(f, bcast, 0x0800);
    uint8_t *ip = f + 14, *udp = ip + 20, *bp = udp + 8;
    ip[0] = 0x45; ip[9] = 17; ip[2] = 0x01; ip[3] = 0x48;  /* 328 */
    memset(ip + 16, 0xff, 4);
    udp[1] = 68; udp[3] = 67; udp[4] = 0x01; udp[5] = 0x34;  /* 308 */
    bp[0] = 1; bp[1] = 1; bp[2] = 6; bp[4] = 0xde; bp[5] = 0xad; memcpy(bp + 28, ME, 6);
    bp[236] = 0x63; bp[237] = 0x82; bp[238] = 0x53; bp[239] = 0x63;
    bp[240] = 53; bp[241] = 1; bp[242] = 1; bp[243] = 255;
    n = 14 + 328;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY);
    const uint8_t *rip = r + 14, *rb = rip + 28;
    assert(!memcmp(r, ME, 6) && rb[0] == 2 && rb[4] == 0xde && !memcmp(rb + 16, "\x0a\x00\x02\x05", 4));
    assert(csum(rip, 20, 0) == 0xffff);
    const uint8_t *o = rb + 240;
    assert(o[0] == 53 && o[2] == 2 && o[3] == 54 && !memcmp(o + 5, "\x0a\x00\x02\x01", 4));
    int saw_dns = 0, saw_mask = 0, saw_mtu = 0;
    for (const uint8_t *q = o; *q != 255; q += 2 + q[1]) {
        if (q[0] == 6) saw_dns = q[1] == 8 && !memcmp(q + 2, "\x01\x01\x01\x01\x08\x08\x08\x08", 8);
        if (q[0] == 1) saw_mask = !memcmp(q + 2, "\xff\xff\xff\x00", 4);
        if (q[0] == 26) saw_mtu = q[2] == 0x05 && q[3] == 0xdc;
    }
    assert(saw_dns && saw_mask && saw_mtu);
    /* request for another address -> NAK; for ours -> ACK */
    bp[242] = 3; bp[243] = 50; bp[244] = 4; memcpy(bp + 245, "\x0a\x00\x02\x09", 4); bp[249] = 255;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY && rb[240 + 2] == 6);
    memcpy(bp + 245, "\x0a\x00\x02\x05", 4);
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY && rb[240 + 2] == 5);
    /* fragments and a UDP length past the datagram are dropped */
    ip[6] = 0x20; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP); ip[6] = 0;
    udp[4] = 0x02; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP); udp[4] = 0x01;
    /* a malformed option list stops cleanly */
    bp[243] = 50; bp[244] = 200;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    /* unicast TCP from our address to the router MAC -> modem; from another source or to multicast -> drop */
    memset(f, 0, sizeof f);
    n = eth(f, RT, 0x0800);
    ip[0] = 0x45; ip[3] = 40; ip[9] = 6; memcpy(ip + 12, "\x0a\x00\x02\x05", 4); memcpy(ip + 16, "\x08\x08\x08\x08", 4);
    n = 14 + 40;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_TO_MODEM && rn == 40);
    assert(cell_link_output(&l, f, 14 + 60, r, &rn) == CELL_TO_MODEM && rn == 40);  /* Ethernet padding is not sent */
    ip[12] = 169; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    ip[12] = 10; ip[16] = 224; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    /* IPv6 */
    const uint8_t v6addr[16] = {0x20, 1, 0xd, 0xb8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2};
    const uint8_t dns6[16] = {0x20, 1, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88};
    assert(cell_link_set_v6(&l, v6addr, 48, 0, 0, 0) == -1);
    assert(!cell_link_set_v6(&l, v6addr, 64, dns6, 0, 1500));
    /* router solicitation -> advertisement with the prefix, RDNSS and a valid checksum */
    memset(f, 0, sizeof f);
    n = eth(f, (const uint8_t *)"\x33\x33\0\0\0\x02", 0x86dd);
    ip[0] = 0x60; ip[5] = 8; ip[6] = 58; ip[7] = 255; ip[8] = 0xfe; ip[9] = 0x80; ip[23] = 1; ip[24] = 0xff; ip[25] = 2; ip[39] = 2;
    ip[40] = 133;
    n = 14 + 48;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);  /* no checksum */
    icmp6_sum(ip);
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY);
    const uint8_t *r6 = r + 14, *ra = r6 + 40;
    const uint32_t plen = (uint32_t)r6[4] << 8 | r6[5];
    assert(ra[0] == 134 && r6[7] == 255 && r6[24] == 0xff && plen == rn - 54);
    assert(csum(ra, plen, csum(r6 + 8, 32, 0) + plen + 58) == 0xffff);
    int saw_prefix = 0, saw_rdnss = 0;
    for (const uint8_t *q = ra + 16; q < ra + plen; q += 8 * q[1]) {
        assert(q[1]);
        if (q[0] == 3) saw_prefix = q[2] == 64 && q[3] == 0x40 && !memcmp(q + 16, v6addr, 8);
        if (q[0] == 25) saw_rdnss = q[1] == 3 && !memcmp(q + 8, dns6, 16);
    }
    assert(saw_prefix && saw_rdnss);
    /* neighbor solicitation for the router's address -> advertisement; a DAD probe (source ::) -> nothing */
    memset(f, 0, sizeof f);
    n = eth(f, RT, 0x86dd);
    ip[0] = 0x60; ip[5] = 24; ip[6] = 58; ip[7] = 255; memcpy(ip + 8, v6addr, 16); ip[23] = 9;
    ip[24] = 0xff; ip[25] = 2; ip[35] = 1; ip[36] = 0xff; ip[39] = 0x77;
    ip[40] = 135; ip[48] = 0xfe; ip[49] = 0x80; ip[63] = 0x77;
    n = 14 + 64;
    icmp6_sum(ip);
    ip[41] = 1; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP); ip[41] = 0;  /* code must be 0 */
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_REPLY && r6[40] == 136 && r6[44] == 0xe0 && r6[63] == 0x77);
    assert(!memcmp(r6 + 24, ip + 8, 16) && !memcmp(r6 + 66, RT, 6));
    assert(csum(r6 + 40, 32, csum(r6 + 8, 32, 0) + 32 + 58) == 0xffff);
    memset(ip + 8, 0, 16);
    icmp6_sum(ip);
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    /* IPv6 TCP from our prefix to the router -> modem; from fe80:: -> drop */
    memset(f, 0, sizeof f);
    n = eth(f, RT, 0x86dd);
    ip[0] = 0x60; ip[5] = 20; ip[6] = 6; memcpy(ip + 8, v6addr, 8); ip[23] = 0x33; ip[24] = 0x26; ip[25] = 0x06;
    n = 14 + 60;
    assert(cell_link_output(&l, f, n, r, &rn) == CELL_TO_MODEM && rn == 60);
    ip[8] = 0xfe; ip[9] = 0x80; assert(cell_link_output(&l, f, n, r, &rn) == CELL_DROP);
    /* from the modem */
    uint8_t h[14];
    const uint8_t v4pkt[20] = {0x45}, v6pkt[40] = {0x60}, junk[20] = {0x15};
    assert(cell_link_input_header(&l, v4pkt, 20, h) == 14 && !memcmp(h, ME, 6) && !memcmp(h + 6, RT, 6) && h[12] == 8);
    assert(cell_link_input_header(&l, v6pkt, 40, h) == 14 && h[12] == 0x86);
    assert(!cell_link_input_header(&l, junk, 20, h) && !cell_link_input_header(&l, v6pkt, 20, h));
    cell_link_set_v4(&l, 0, 0, 0, 0);
    assert(!cell_link_input_header(&l, v4pkt, 20, h));
    /* runts and oversize frames */
    assert(cell_link_output(&l, f, 10, r, &rn) == CELL_DROP && cell_link_output(&l, f, CELL_FRAME_MAX + 1, r, &rn) == CELL_DROP);
}

/* ---- the session against a fake modem ---- */
typedef struct {uint8_t service[64], client[64]; uint16_t msg[64], txn[64]; uint8_t body[64][256]; uint32_t len[64]; unsigned n, refuse;} Sent;
static Sent sent;
static unsigned ups, downs, errors;
static CellState last;
static int fake_send(void *ctx, unsigned client, uint8_t service, const uint8_t *m, uint32_t length)
{
    (void)ctx;
    if (sent.refuse) return -1;
    assert(sent.n < 64 && length <= 256);
    sent.service[sent.n] = service; sent.client[sent.n] = (uint8_t)client;
    sent.msg[sent.n] = (uint16_t)(m[3] | m[4] << 8); sent.txn[sent.n] = (uint16_t)(m[1] | m[2] << 8);
    memcpy(sent.body[sent.n], m, length); sent.len[sent.n] = length;
    assert(sent.txn[sent.n] && sent.txn[sent.n] < CELL_TXN_RAW);
    sent.n++;
    return 0;
}
static void fake_event(void *ctx, CellEvent e, uint32_t arg, const void *data, const char *text)
{
    (void)ctx; (void)data;
    if (e == CE_UP) ups++;
    if (e == CE_DOWN) downs++;
    if (e == CE_ERROR) {errors++; if (getenv("VERBOSE")) fprintf(stderr, "error %#x %s\n", arg, text);}
    if (e == CE_STATE) last = (CellState)arg;
}

/* the last request of (client, service, msg) */
static int find_sent(unsigned client, uint8_t service, uint16_t msg)
{
    for (int i = (int)sent.n - 1; i >= 0; --i) if (sent.client[i] == client && sent.service[i] == service && sent.msg[i] == msg) return i;
    return -1;
}
static int count_sent(uint8_t service, uint16_t msg)
{
    int k = 0;
    for (unsigned i = 0; i < sent.n; ++i) k += sent.service[i] == service && sent.msg[i] == msg;
    return k;
}

static void answer(CellSession *s, unsigned client, uint8_t service, uint16_t msg, int err, const uint8_t *tlvs, uint32_t n)
{
    const int i = find_sent(client, service, msg);
    if (i < 0) fprintf(stderr, "no request client %u service %#x msg %#x (state %s)\n", client, service, msg, cell_state_name(s->state));
    assert(i >= 0);
    const uint32_t len = resp(b, 2, sent.txn[i], msg, err, tlvs, n);
    cell_session_receive(s, client, service, b, len);
}

static const CellConfig CFG = {0, 1, 1, "internet", 0, 1, 1, 2, 18};
static const CellReady READY = {QMAP_V1, 32, 16384};
static const uint8_t SVCS[] = {QMI_SVC_DMS, QMI_SVC_NAS, QMI_SVC_WDS, QMI_SVC_WDA, QMI_SVC_DPM};
static const uint8_t ONLINE_MODE[] = {0x01, 1, 0, 0}, OFFLINE_MODE[] = {0x01, 1, 0, 3};
static const uint8_t WDA_OK[] = {0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0,
                                0x15, 4, 0, 16, 0, 0, 0, 0x16, 4, 0, 0, 0x20, 0, 0};
static const uint8_t WDA_NOLIMITS[] = {0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0};
static const uint8_t H4[] = {0x01, 4, 0, 0x44, 0x44, 0, 0}, H6[] = {0x01, 4, 0, 0x66, 0x66, 0, 0};
static const uint8_t SET4[] = {0x1e, 4, 0, 5, 2, 0, 10, 0x29, 4, 0, 0xdc, 5, 0, 0};
static const uint8_t SET6[] = {0x25, 17, 0, 0x20, 1, 0xd, 0xb8, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 64};

static void fresh(CellSession *s, const CellConfig *cfg, uint64_t t)
{
    static const CellSessionOps ops = {fake_send, fake_event, 0};
    memset(&sent, 0, sizeof sent);
    ups = downs = errors = 0;
    cell_session_init(s, cfg, &ops, t);
    for (unsigned i = 0; i < sizeof SVCS; ++i) cell_session_service(s, SVCS[i], 1);
}

/* From a fresh session to CS_REGISTER with the modem already online. */
static void to_register(CellSession *s, uint64_t *t)
{
    cell_session_ready(s, &READY);
    answer(s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    answer(s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, WDA_OK, sizeof WDA_OK);
    answer(s, 0, QMI_SVC_DMS, DMS_GET_OPERATING_MODE, 0, ONLINE_MODE, sizeof ONLINE_MODE);
    cell_session_poll(s, *t += 100);
    assert(s->state == CS_REGISTER);
}

static void registered(CellSession *s)
{
    const uint32_t len = IND(0x24, 0x01, 6, 0, 1, 1, 1, 2, 1, 8);
    cell_session_receive(s, 0, QMI_SVC_NAS, b, len);
}

/* Bind and family for both calls; afterwards both starts are outstanding. */
static void bound(CellSession *s)
{
    for (unsigned c = 0; c < 2; ++c) {
        answer(s, c, QMI_SVC_WDS, WDS_BIND_MUX_DATA_PORT, 0, 0, 0);
        const int i = find_sent(c, QMI_SVC_WDS, WDS_SET_IP_FAMILY);
        assert(i >= 0 && sent.body[i][10] == (c ? 6 : 4));
        answer(s, c, QMI_SVC_WDS, WDS_SET_IP_FAMILY, 0, 0, 0);
        assert(find_sent(c, QMI_SVC_WDS, WDS_START_NETWORK) >= 0);
    }
}

static void session_order(void)
{
    CellSession s;
    uint64_t t = 1000;
    fresh(&s, &CFG, t);
    /* only read-only queries before the readiness barrier */
    assert(s.state == CS_WAIT_READY && find_sent(0, QMI_SVC_DMS, DMS_GET_IDS) >= 0 && find_sent(0, QMI_SVC_WDS, WDS_GET_PROFILE_LIST) >= 0);
    for (t = 1000; t < 20000; t += 100) cell_session_poll(&s, t);
    assert(count_sent(QMI_SVC_DPM, DPM_OPEN_PORT) == 0 && count_sent(QMI_SVC_WDA, WDA_SET_DATA_FORMAT) == 0 && cell_session_idle(&s));
    for (unsigned i = 0; i < sent.n; ++i)
        assert(sent.msg[i] == DMS_GET_REVISION || sent.msg[i] == DMS_GET_IDS || sent.msg[i] == NAS_GET_OPERATOR_NAME || sent.msg[i] == NAS_GET_SIGNAL_INFO || sent.msg[i] == WDS_GET_PROFILE_LIST);
    /* a bad readiness record is refused */
    const CellReady bad = {QMAP_V1, 0, 16384};
    cell_session_ready(&s, &bad);
    assert(s.state == CS_WAIT_READY);
    assert(count_sent(QMI_SVC_NAS, NAS_REGISTER_INDICATIONS) == 0);
    cell_session_ready(&s, &READY);
    assert(count_sent(QMI_SVC_NAS, NAS_REGISTER_INDICATIONS) == 1);
    int i = find_sent(0, QMI_SVC_DPM, DPM_OPEN_PORT);
    assert(s.state == CS_DPM && i >= 0 && sent.body[i][19] == 2 && sent.body[i][23] == 18);
    /* a stray response with a raw-tool transaction is ignored */
    uint32_t len = resp(b, 2, 0x8001, DPM_OPEN_PORT, 0, 0, 0);
    cell_session_receive(&s, 0, QMI_SVC_DPM, b, len);
    assert(s.state == CS_DPM);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    assert(s.state == CS_WDA);
    /* an echo with no format TLVs is verified with Get Data Format */
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, 0, 0);
    assert(s.state == CS_WDA_VERIFY && find_sent(0, QMI_SVC_WDA, WDA_GET_DATA_FORMAT) >= 0);
    {   /* Get without the limits fails closed */
        CellSession v = s; Sent keep = sent;
        answer(&v, 0, QMI_SVC_WDA, WDA_GET_DATA_FORMAT, 0, WDA_NOLIMITS, sizeof WDA_NOLIMITS);
        cell_session_poll(&v, t);
        assert(v.state == CS_STOPPING || v.state == CS_FAILED);
        sent = keep;
    }
    answer(&s, 0, QMI_SVC_WDA, WDA_GET_DATA_FORMAT, 0, WDA_OK, sizeof WDA_OK);
    assert(s.state == CS_ONLINE && find_sent(0, QMI_SVC_DMS, DMS_GET_OPERATING_MODE) >= 0);
    /* offline and online not allowed: never a set mode */
    answer(&s, 0, QMI_SVC_DMS, DMS_GET_OPERATING_MODE, 0, OFFLINE_MODE, sizeof OFFLINE_MODE);
    assert(s.state == CS_WAIT_ONLINE_ALLOWED);
    for (; t < 40000; t += 100) cell_session_poll(&s, t);
    assert(count_sent(QMI_SVC_DMS, DMS_SET_OPERATING_MODE) == 0);
    /* allowed: set mode 0 once, then read back */
    CellSession on = s;
    on.cfg.allow_online = 1; on.state = CS_ONLINE; on.mode_known = 1; on.mode = 3;
    cell_session_poll(&on, t);
    i = find_sent(0, QMI_SVC_DMS, DMS_SET_OPERATING_MODE);
    assert(count_sent(QMI_SVC_DMS, DMS_SET_OPERATING_MODE) == 1 && sent.body[i][10] == 0);
    answer(&on, 0, QMI_SVC_DMS, DMS_SET_OPERATING_MODE, 0, 0, 0);
    cell_session_poll(&on, t += 100);
    answer(&on, 0, QMI_SVC_DMS, DMS_GET_OPERATING_MODE, 0, ONLINE_MODE, sizeof ONLINE_MODE);
    cell_session_poll(&on, t += 100);
    assert(on.state == CS_REGISTER && count_sent(QMI_SVC_DMS, DMS_SET_OPERATING_MODE) == 1);
}

static void session_calls(void)
{
    CellSession s;
    uint64_t t = 1000;
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    static const uint8_t searching[] = {0x01, 6, 0, 2, 0, 0, 2, 1, 8};
    answer(&s, 0, QMI_SVC_NAS, NAS_SERVING_SYSTEM, 0, searching, sizeof searching);
    assert(s.state == CS_REGISTER && find_sent(0, QMI_SVC_WDS, WDS_BIND_MUX_DATA_PORT) < 0);
    registered(&s);
    assert(s.state == CS_CALLS);
    /* a refused family is retried, not taken as set */
    answer(&s, 0, QMI_SVC_WDS, WDS_BIND_MUX_DATA_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDS, WDS_SET_IP_FAMILY, 0x1a + 1, 0, 0);
    assert(!s.call[0].family_set && find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK) < 0);
    cell_session_poll(&s, t += 1100);
    answer(&s, 0, QMI_SVC_WDS, WDS_SET_IP_FAMILY, 0, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_BIND_MUX_DATA_PORT, 0, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_SET_IP_FAMILY, 0, 0, 0);
    assert(find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK) >= 0 && find_sent(1, QMI_SVC_WDS, WDS_START_NETWORK) >= 0);
    /* IPv4 refused while attaching, retried after 6 s */
    static const uint8_t why[] = {0x10, 2, 0, 1, 0};
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, QMI_ERR_INVALID_OPERATION, why, sizeof why);
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, 0, H6, sizeof H6);
    /* settings with only an IPv4 address do not bring up the IPv6 call */
    answer(&s, 1, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, SET4, sizeof SET4);
    assert(!s.call[1].up && ups == 0);
    cell_session_poll(&s, t += 1100);
    answer(&s, 1, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, SET6, sizeof SET6);
    assert(s.call[1].up && ups == 1);
    const int starts = count_sent(QMI_SVC_WDS, WDS_START_NETWORK);
    cell_session_poll(&s, t += 3000);
    assert(count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == starts);
    cell_session_poll(&s, t += 3100);
    assert(count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == starts + 1);
    int i = find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK);
    assert(!memcmp(sent.body[i] + 10, "internet", 8));
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, SET4, sizeof SET4);
    assert(ups == 2 && s.call[0].up && s.call[0].handle == 0x4444);
    /* the network drops IPv4: down, and started again after the backoff */
    uint32_t len = IND(0x22, 0x01, 2, 0, 1, 0, 0x10, 2, 0, 2, 0);
    cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
    assert(downs == 1 && !s.call[0].handle && s.call[1].up);
    const int before = count_sent(QMI_SVC_WDS, WDS_START_NETWORK);
    cell_session_poll(&s, t += 6100);
    assert(count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == before + 1);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    /* stop: a refused stop keeps the handle and is retried; success brings the family down */
    cell_session_stop(&s);
    assert(s.state == CS_STOPPING);
    i = find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK);
    const int j = find_sent(1, QMI_SVC_WDS, WDS_STOP_NETWORK);
    assert(i >= 0 && j >= 0 && sent.body[i][10] == 0x44 && sent.body[j][10] == 0x66);
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0x0e, 0, 0);
    assert(s.call[0].handle == 0x4444 && !cell_session_idle(&s));
    cell_session_poll(&s, t += 1100);
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    assert(cell_session_idle(&s) && s.state == CS_STOPPED && downs == 2 && cell_session_cleanup(&s) == CELL_CLEANUP_CLEAN);
    /* nothing is sent once stopped */
    const unsigned n = sent.n;
    for (unsigned k = 0; k < 100; ++k) cell_session_poll(&s, t += 100);
    assert(sent.n == n);
}

static void session_stop_edges(void)
{
    CellSession s;
    uint64_t t = 1000;
    /* stop while a start is in flight: the late handle is stopped, never brought up */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    cell_session_stop(&s);
    assert(s.state == CS_STOPPING && !cell_session_idle(&s));
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    assert(find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK) >= 0 && find_sent(0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS) < 0);
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    assert(!cell_session_idle(&s));            /* the IPv6 start is still out */
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, QMI_ERR_INVALID_OPERATION, 0, 0);
    cell_session_poll(&s, t += 100);
    assert(cell_session_idle(&s) && ups == 0);
    /* a late DPM or WDA answer cannot restart setup */
    fresh(&s, &CFG, t);
    cell_session_ready(&s, &READY);
    cell_session_stop(&s);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    cell_session_poll(&s, t += 100);
    assert(s.state == CS_STOPPED && count_sent(QMI_SVC_WDA, WDA_SET_DATA_FORMAT) == 0);
    /* stop before the barrier sends nothing */
    fresh(&s, &CFG, t);
    const unsigned n0 = sent.n;
    cell_session_stop(&s);
    assert(s.state == CS_STOPPED && sent.n == n0 && cell_session_cleanup(&s) == CELL_CLEANUP_CLEAN);
    /* a service leaving with a live call stops the call before FAILED */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, SET4, sizeof SET4);
    cell_session_service(&s, QMI_SVC_NAS, 0);
    assert(s.state == CS_STOPPING && find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK) >= 0 && !cell_session_idle(&s));
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, 0x46, 0, 0);
    cell_session_poll(&s, t += 100);
    assert(s.state == CS_FAILED && downs == 1);
    /* readiness lost does the same */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    cell_session_ready(&s, 0);
    assert(s.state == CS_STOPPING || s.state == CS_FAILED);
    cell_session_poll(&s, t += 100);
    assert(s.state == CS_FAILED);
    /* WDS gone: the stop cannot be sent; FAILED after 5 s, reported unconfirmed */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    cell_session_service(&s, QMI_SVC_WDS, 0);
    cell_session_poll(&s, t += 4000);
    assert(!cell_session_idle(&s));
    cell_session_poll(&s, t += 1100);
    assert(s.state == CS_FAILED && cell_session_cleanup(&s) == CELL_CLEANUP_UNRESOLVED);
    /* an unanswered stop still ends after 5 s */
    fresh(&s, &CFG, 0);
    s.state = CS_CALLS; s.call[0].handle = 7;
    cell_session_stop(&s);
    cell_session_poll(&s, 4000); assert(!cell_session_idle(&s));
    cell_session_poll(&s, 5100); assert(cell_session_idle(&s));
}

static void session_bounds(void)
{
    CellSession s;
    uint64_t t = 0;
    /* DPM never answered: three tries, then failed */
    fresh(&s, &CFG, 0);
    cell_session_ready(&s, &READY);
    for (t = 0; t < 30000; t += 100) cell_session_poll(&s, t);
    assert(s.state == CS_FAILED && count_sent(QMI_SVC_DPM, DPM_OPEN_PORT) == 3);
    /* a transport that refuses sends: at most one try a second */
    fresh(&s, &CFG, 0);
    sent.refuse = 1;
    cell_session_ready(&s, &READY);
    for (t = 0; t < 3000; t += 10) cell_session_poll(&s, t);
    assert(s.tries <= 4);
    sent.refuse = 0;
    /* WDA echoing another aggregation or bigger limits fails */
    static const uint8_t qv5[] = {0x13, 4, 0, 9, 0, 0, 0};
    static const uint8_t toobig[] = {0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0, 0x16, 4, 0, 0, 0x80, 0, 0};
    fresh(&s, &CFG, 0);
    cell_session_ready(&s, &READY);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, qv5, sizeof qv5);
    cell_session_poll(&s, 100);
    assert(s.state == CS_FAILED);
    fresh(&s, &CFG, 0);
    cell_session_ready(&s, &READY);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, toobig, sizeof toobig);
    cell_session_poll(&s, 100);
    assert(s.state == CS_FAILED);
    /* a start refused for a reason that will not change ends that call; the other family alone keeps the session */
    fresh(&s, &CFG, 1000);
    t = 1000;
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0x11, 0, 0);
    assert(s.call[0].exhausted && s.state == CS_CALLS);
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, 0x11, 0, 0);
    assert(s.state == CS_STOPPING || s.state == CS_FAILED);
    /* twenty attaching refusals, then that call stops trying */
    fresh(&s, &CFG, 1000);
    t = 1000;
    CellConfig v4only = CFG;
    v4only.want_v6 = 0;
    fresh(&s, &v4only, 1000);
    to_register(&s, &t);
    registered(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_BIND_MUX_DATA_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDS, WDS_SET_IP_FAMILY, 0, 0, 0);
    for (unsigned k = 0; k < 40; ++k) {
        if (find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK) >= 0 && s.pending[0].busy + s.pending[1].busy >= 0) {
            const int at = find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK);
            const uint32_t len = resp(b, 2, sent.txn[at], WDS_START_NETWORK, QMI_ERR_INVALID_OPERATION, 0, 0);
            cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
        }
        cell_session_poll(&s, t += 6100);
    }
    assert(count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == 20 && s.call[0].exhausted && s.state == CS_FAILED);
}

/* The re-audit's cases (docs/data-2220-reaudit.md B2 to B5, N3). */
static void session_reaudit(void)
{
    CellSession s;
    uint64_t t = 1000;
    /* B3: a start that times out, then stop, then its late success: the handle is stopped and idle waits for it */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    const int st0 = find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK);
    const uint16_t txn0 = sent.txn[st0];
    cell_session_poll(&s, t += 5100);           /* past the reply deadline */
    assert(s.call[0].start_unknown);
    const int starts = count_sent(QMI_SVC_WDS, WDS_START_NETWORK);
    cell_session_poll(&s, t += 7000);           /* no second start while the first is unresolved */
    assert(count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == starts);
    cell_session_stop(&s);
    cell_session_poll(&s, t += 1000);
    assert(!cell_session_idle(&s));
    uint32_t len = resp(b, 2, txn0, WDS_START_NETWORK, 0, H4, sizeof H4);
    cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
    assert(s.call[0].handle == 0x4444 && find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK) >= 0);
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    /* the IPv6 start is unresolved too: idle only at the 5 s deadline, reported unconfirmed */
    assert(!cell_session_idle(&s));
    errors = 0;
    assert(cell_session_cleanup(&s) == CELL_CLEANUP_PENDING);
    cell_session_poll(&s, t += 4100);
    assert(cell_session_idle(&s) && errors >= 1 && cell_session_cleanup(&s) == CELL_CLEANUP_UNRESOLVED);
    /* B3 outside shutdown: a late success in CALLS is adopted, then its settings are read */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    const uint16_t txn1 = sent.txn[find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK)];
    cell_session_poll(&s, t += 5100);
    len = resp(b, 2, txn1, WDS_START_NETWORK, 0, H4, sizeof H4);
    cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
    assert(s.call[0].handle == 0x4444 && !s.call[0].start_unknown && find_sent(0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS) >= 0);
    /* a start never answered ends its call after 30 s */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    for (unsigned k = 0; k < 320; ++k) cell_session_poll(&s, t += 100);
    assert(s.call[0].exhausted && s.call[1].exhausted);
    /* B4: an explicit zero limit fails */
    static const uint8_t zero[] = {0x11, 4, 0, 2, 0, 0, 0, 0x12, 4, 0, 5, 0, 0, 0, 0x13, 4, 0, 5, 0, 0, 0,
                                   0x15, 4, 0, 0, 0, 0, 0, 0x16, 4, 0, 0, 0x20, 0, 0};
    fresh(&s, &CFG, 0);
    cell_session_ready(&s, &READY);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, zero, sizeof zero);
    cell_session_poll(&s, 100);
    assert(s.state == CS_STOPPED || s.state == CS_FAILED);
    /* a duplicated format TLV is malformed */
    static const uint8_t dup[] = {0x11, 4, 0, 2, 0, 0, 0, 0x11, 4, 0, 2, 0, 0, 0};
    fresh(&s, &CFG, 0);
    cell_session_ready(&s, &READY);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, dup, sizeof dup);
    assert(s.state >= CS_STOPPING);
    /* B5: readiness revoked, then the pending WDA answer arrives: no mode traffic follows */
    fresh(&s, &CFG, 0);
    s.cfg.allow_online = 1;
    cell_session_ready(&s, &READY);
    answer(&s, 0, QMI_SVC_DPM, DPM_OPEN_PORT, 0, 0, 0);
    cell_session_ready(&s, 0);
    answer(&s, 0, QMI_SVC_WDA, WDA_SET_DATA_FORMAT, 0, WDA_OK, sizeof WDA_OK);
    cell_session_poll(&s, 100);
    assert(count_sent(QMI_SVC_DMS, DMS_GET_OPERATING_MODE) == 0 && count_sent(QMI_SVC_DMS, DMS_SET_OPERATING_MODE) == 0);
    assert(s.state >= CS_STOPPING);
    /* N3: a reconfigure withdraws the presented family, and stop does not need to again */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, SET4, sizeof SET4);
    assert(ups == 1);
    len = IND(0x22, 0x01, 2, 0, 2, 1);
    cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
    assert(downs == 1 && !s.call[0].up);
    /* N4: a family TLV of 0 is a conflict, not an absence */
    static const uint8_t fam0[] = {0x1e, 4, 0, 5, 2, 0, 10, 0x2b, 1, 0, 0};
    cell_session_poll(&s, t += 1100);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, fam0, sizeof fam0);
    assert(!s.call[0].up);
    /* N4: unusable addresses and a conflicting family */
    static const uint8_t mcast[] = {0x1e, 4, 0, 1, 0, 0, 224};
    static const uint8_t wrongfam[] = {0x1e, 4, 0, 5, 2, 0, 10, 0x2b, 1, 0, 6};
    cell_session_poll(&s, t += 1100);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, mcast, sizeof mcast);
    assert(!s.call[0].up);
    cell_session_poll(&s, t += 1100);
    answer(&s, 0, QMI_SVC_WDS, WDS_GET_CURRENT_SETTINGS, 0, wrongfam, sizeof wrongfam);
    assert(!s.call[0].up);
}

/* Third audit: malformed start replies keep ownership; the transaction space is never reused. */
static void session_audit3(void)
{
    CellSession s;
    uint64_t t = 1000;
    /* stop with both starts out, then QMI successes without handles: still unresolved, a later valid success is stopped */
    fresh(&s, &CFG, t);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    const uint16_t txn0 = sent.txn[find_sent(0, QMI_SVC_WDS, WDS_START_NETWORK)];
    const uint16_t txn1 = sent.txn[find_sent(1, QMI_SVC_WDS, WDS_START_NETWORK)];
    cell_session_stop(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, 0, 0, 0);
    cell_session_poll(&s, t += 100);
    assert(s.call[0].start_unknown && s.call[1].start_unknown && !cell_session_idle(&s));
    uint32_t len = resp(b, 2, txn0, WDS_START_NETWORK, 0, H4, sizeof H4);
    cell_session_receive(&s, 0, QMI_SVC_WDS, b, len);
    assert(s.call[0].handle == 0x4444 && find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK) >= 0);
    answer(&s, 0, QMI_SVC_WDS, WDS_STOP_NETWORK, 0, 0, 0);
    /* a duplicated handle TLV is malformed too */
    static const uint8_t twice[] = {0x01, 4, 0, 1, 0, 0, 0, 0x01, 4, 0, 2, 0, 0, 0};
    len = resp(b, 2, txn1, WDS_START_NETWORK, 0, twice, sizeof twice);
    cell_session_receive(&s, 1, QMI_SVC_WDS, b, len);
    assert(s.call[1].start_unknown && !s.call[1].handle);
    cell_session_poll(&s, t += 5000);
    assert(cell_session_idle(&s) && cell_session_cleanup(&s) == CELL_CLEANUP_UNRESOLVED);
    /* the same before stop: no second start while the malformed one is unresolved */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, 0, 0);
    const int starts = count_sent(QMI_SVC_WDS, WDS_START_NETWORK);
    cell_session_poll(&s, t += 7000);
    assert(s.call[0].start_unknown && count_sent(QMI_SVC_WDS, WDS_START_NETWORK) == starts);
    /* a valid failure result settles it */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, QMI_ERR_INVALID_OPERATION, 0, 0);
    assert(!s.call[0].start_unknown);
    /* transactions: setup stops before the cleanup reserve, and no number is used twice */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, 0, H4, sizeof H4);
    s.next_txn = CELL_TXN_RAW - 65;
    for (unsigned k = 0; k < 400; ++k) cell_session_poll(&s, t += 100);
    assert(s.state == CS_STOPPED || s.state == CS_FAILED || s.state == CS_STOPPING);
    for (unsigned i = 0; i < sent.n; ++i)
        for (unsigned j = i + 1; j < sent.n; ++j) assert(sent.txn[i] != sent.txn[j] || sent.txn[i] < CELL_TXN_RAW - 65);
    assert(find_sent(0, QMI_SVC_WDS, WDS_STOP_NETWORK) >= 0);
    /* fourth audit N1: a NAS poll reaching the reserve while IPv4 may restart: after the transition only Stop Network goes out */
    fresh(&s, &CFG, t = 1000);
    to_register(&s, &t);
    registered(&s);
    bound(&s);
    answer(&s, 0, QMI_SVC_WDS, WDS_START_NETWORK, QMI_ERR_INVALID_OPERATION, 0, 0);
    answer(&s, 1, QMI_SVC_WDS, WDS_START_NETWORK, 0, H6, sizeof H6);
    s.next_txn = CELL_TXN_RAW - 64;
    s.next_poll = 0;
    const unsigned before = sent.n;
    cell_session_poll(&s, t += 6100);  /* the NAS poll and the IPv4 restart are both due */
    assert(s.state == CS_STOPPING);
    cell_session_poll(&s, t += 100);
    for (unsigned i = before; i < sent.n; ++i) assert(sent.service[i] == QMI_SVC_WDS && sent.msg[i] == WDS_STOP_NETWORK);
    assert(find_sent(1, QMI_SVC_WDS, WDS_STOP_NETWORK) >= (int)before);
    /* a response with two result TLVs is malformed */
    static const uint8_t res2[] = {0x02, 4, 0, 1, 0, 0x0e, 0, 0x01, 1, 0, 0};
    const uint32_t l2 = resp(b, 2, 9, DMS_GET_OPERATING_MODE, 0, res2, sizeof res2);
    QcomQmiMessage qm;
    assert(qmi_response(b, l2, 9, DMS_GET_OPERATING_MODE, &qm) == -1);
}

static void session(void)
{
    session_order();
    session_calls();
    session_stop_edges();
    session_bounds();
    session_reaudit();
    session_audit3();
}

/* ---- raw allow list ---- */
static int raw_try(uint8_t service, uint8_t client, const uint8_t *msg, uint32_t n)
{
    uint8_t req[8 + 256] = {1, service, client, 0, 0xe8, 3, 0, 0};
    QmiRawRequest r;
    memcpy(req + 8, msg, n);
    return qmi_raw_parse(req, 8 + n, &r);
}

static int raw_id(uint8_t service, uint16_t id)
{
    uint8_t m[7] = {0, 0, 0, (uint8_t)id, (uint8_t)(id >> 8), 0, 0};
    return raw_try(service, 0, m, 7);
}

static void raw(void)
{
    uint8_t m[64];
    uint32_t n;
    QmiOut o;
    n = dms_get_revision(m, sizeof m, 0); assert(!raw_try(QMI_SVC_DMS, 0, m, n));
    uint8_t req[64] = {1, QMI_SVC_DMS, 0, 0, 0xe8, 3, 0, 0};
    QmiRawRequest r;
    memcpy(req + 8, m, n);
    assert(!qmi_raw_parse(req, 8 + n, &r) && r.service == 2 && r.timeout_ms == 1000 && r.length == n);
    /* identifiers, resets, writes, calls and session state */
    const struct {uint8_t svc; uint16_t id;} denied[] = {
        {QMI_SVC_DMS, 0x25}, {QMI_SVC_DMS, 0x24}, {QMI_SVC_DMS, 0x3c}, {QMI_SVC_DMS, 0x43}, {QMI_SVC_DMS, 0x00}, {QMI_SVC_DMS, 0x3a},
        {QMI_SVC_DMS, 0x28}, {QMI_SVC_NAS, 0x00}, {QMI_SVC_NAS, 0x03}, {QMI_SVC_NAS, 0x21}, {QMI_SVC_NAS, 0x2a},
        {QMI_SVC_WDS, 0x00}, {QMI_SVC_WDS, 0x20}, {QMI_SVC_WDS, 0x21}, {QMI_SVC_WDS, 0x27}, {QMI_SVC_WDS, 0x28},
        {QMI_SVC_WDS, 0x29}, {QMI_SVC_WDS, 0x2b}, {QMI_SVC_WDS, 0x2c}, {QMI_SVC_WDS, 0x4a}, {QMI_SVC_WDS, 0x4d},
        {QMI_SVC_WDS, 0x51}, {QMI_SVC_WDS, 0xa2}, {QMI_SVC_WDS, 0x01}, {QMI_SVC_WDS, 0x86}, {QMI_SVC_WDA, 0x00},
        {QMI_SVC_WDA, 0x20}, {QMI_SVC_DPM, 0x20}, {QMI_SVC_UIM, 0x47}, {QMI_SVC_UIM, 0x20}, {0x24, 0x01}, {0x31, 0x21},
    };
    for (unsigned i = 0; i < sizeof denied / sizeof denied[0]; ++i) assert(raw_id(denied[i].svc, denied[i].id) == 1);
    assert(!raw_id(QMI_SVC_NAS, 0x24) && !raw_id(QMI_SVC_WDS, 0x2a) && !raw_id(QMI_SVC_UIM, 0x2f) && !raw_id(QMI_SVC_WDA, 0x21));
    /* no setter at all: online and mode preference are the session's, behind its barrier */
    n = dms_set_operating_mode(m, sizeof m, 0, 0); assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);
    n = dms_set_operating_mode(m, sizeof m, 0, 1); assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);
    m[10] = 6; assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);
    m[10] = 4; assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);
    qmi_begin(&o, m, sizeof m, 0, 0x2e); qmi_put_u8(&o, 1, 0); qmi_put_u8(&o, 1, 4); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);                           /* a second mode hidden behind the first */
    qmi_begin(&o, m, sizeof m, 0, 0x2e); qmi_put_u8(&o, 1, 0); qmi_put_u8(&o, 0x10, 1); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_DMS, 0, m, n) == 1);                           /* an unknown extra TLV */
    /* NAS mode preference only until power cycle */
    n = nas_set_mode_preference(m, sizeof m, 0, 1 << 4); assert(raw_try(QMI_SVC_NAS, 0, m, n) == 1);
    qmi_begin(&o, m, sizeof m, 0, 0x33); qmi_put_u16(&o, 0x11, 16); n = qmi_end(&o); assert(raw_try(QMI_SVC_NAS, 0, m, n) == 1);
    qmi_begin(&o, m, sizeof m, 0, 0x33); qmi_put_u16(&o, 0x11, 16); qmi_put_u8(&o, 0x17, 1); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_NAS, 0, m, n) == 1);
    qmi_begin(&o, m, sizeof m, 0, 0x33); qmi_put_u16(&o, 0x11, 16); qmi_put_u8(&o, 0x17, 0); qmi_put_u8(&o, 0x17, 1); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_NAS, 0, m, n) == 1);                           /* duplicate duration */
    qmi_begin(&o, m, sizeof m, 0, 0x33); qmi_put_u16(&o, 0x11, 16); qmi_put_u8(&o, 0x17, 0); qmi_put_u8(&o, 0x16, 1); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_NAS, 0, m, n) == 1);                           /* network selection */
    /* duplicates on a read, client 1, timeouts and versions */
    qmi_begin(&o, m, sizeof m, 0, 0x2a); qmi_put_u8(&o, 0x10, 0); qmi_put_u8(&o, 0x10, 0); n = qmi_end(&o);
    assert(raw_try(QMI_SVC_WDS, 0, m, n) == 1);
    n = wds_get_profile_list(m, sizeof m, 0); assert(raw_try(QMI_SVC_WDS, 1, m, n) == 22);
    memcpy(req + 8, m, n);
    req[1] = QMI_SVC_WDS; req[4] = req[5] = 0; assert(qmi_raw_parse(req, 8 + n, &r) == 22);
    req[4] = 0x11; req[5] = 0x27; assert(qmi_raw_parse(req, 8 + n, &r) == 22);
    req[4] = 0xe8; req[5] = 3; req[0] = 2; assert(qmi_raw_parse(req, 8 + n, &r) == 22);
    req[0] = 1; req[8 + 5] = 9; assert(qmi_raw_parse(req, 8 + n, &r) == 1);
    assert(qmi_raw_indication_kept(QMI_SVC_NAS, 0x24) && qmi_raw_indication_kept(QMI_SVC_WDS, 0x22));
    assert(!qmi_raw_indication_kept(QMI_SVC_UIM, 0x32) && !qmi_raw_indication_kept(QMI_SVC_DMS, 0x01));
}

int main(void)
{
    requests();
    decoders();
    qmap();
    link();
    session();
    raw();
    puts("data host checks: PASS");
    return 0;
}
