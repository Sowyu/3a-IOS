/* Modem 2189 audit finding 1: real QRTR (QcomQrtr.c), the real data service cache (DataServices.c) and the real session
 * (CellSession.c), fed whole QRTR packets in the order one GLINK batch can deliver them. The receive callback below makes the same
 * calls in the same order as sweet_data.c sd_receive (data_services_step, lookup, session receive); discovery() is
 * sweet_data_discovery. ready_now stands for the owner's USIM and IPA evidence. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "QcomQrtr.h"
#include "DataServices.h"
#include "CellSession.h"

#define LOCAL0 0x4010u
#define MODEM 0u
static QcomQrtr q;
static DataServices svcs;
static CellSession s;
static int ready_now;
static const CellReady READY = {QMAP_V1, 16, 8192};
static struct {uint8_t service[128]; uint16_t msg[128], txn[128]; unsigned n;} out;

static int link_send(void *ctx, const uint8_t *packet, uint32_t length) {(void)ctx; (void)packet; (void)length; return 0;}

static int session_send(void *ctx, unsigned client, uint8_t service, const uint8_t *msg, uint32_t length)
{
    (void)ctx;
    uint32_t node, port;
    if (data_services_endpoint(&svcs, service, &node, &port)) return -1;  /* as sd_send: only through the cache */
    assert(out.n < 128);
    out.service[out.n] = service; out.msg[out.n] = (uint16_t)(msg[3] | msg[4] << 8); out.txn[out.n] = (uint16_t)(msg[1] | msg[2] << 8);
    out.n++;
    return qcom_qrtr_send_data(&q, client ? LOCAL0 + 1 : LOCAL0, node, port, msg, length);
}
static void session_event(void *ctx, CellEvent e, uint32_t arg, const void *data, const char *text) {(void)ctx; (void)e; (void)arg; (void)data; (void)text;}

static void receive(void *ctx, QcomQrtr *qr, uint32_t port, uint32_t node, uint32_t from, const uint8_t *data, uint32_t length)
{
    (void)ctx;
    data_services_step(&svcs, qr, &s, ready_now, &READY, 0, 0);
    const int service = data_services_lookup(&svcs, node, from);
    if (service < 0) return;
    cell_session_receive(&s, port == LOCAL0 + 1, (uint8_t)service, data, length);
}
static void discovery(void) {data_services_step(&svcs, &q, &s, ready_now, &READY, 0, 0);}

static void put32(uint8_t *p, uint32_t v) {p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);}

/* one QRTR packet from the modem into the worker's receive path */
static void deliver(uint32_t type, uint32_t src_port, uint32_t dst_port, const uint8_t *payload, uint32_t n)
{
    uint8_t p[512] = {0};
    put32(p, 1); put32(p + 4, type); put32(p + 8, MODEM); put32(p + 12, src_port); put32(p + 20, n);
    put32(p + 24, 1); put32(p + 28, dst_port);
    memcpy(p + 32, payload, n);
    assert(qcom_qrtr_receive(&q, p, 32 + ((n + 3) & ~3u)) == 0);
}
static void server(uint32_t type, uint8_t service, uint32_t port, int hook)
{
    uint8_t c[20];
    put32(c, type); put32(c + 4, service); put32(c + 8, 1); put32(c + 12, MODEM); put32(c + 16, port);
    deliver(type, QCOM_QRTR_PORT_CTRL, QCOM_QRTR_PORT_CTRL, c, sizeof c);
    if (hook) discovery();  /* the modem runtime calls sweet_data_discovery after each control packet */
}
static void reply(uint32_t port, uint16_t msg, uint16_t txn)
{
    const uint8_t m[] = {2, (uint8_t)txn, (uint8_t)(txn >> 8), (uint8_t)msg, (uint8_t)(msg >> 8), 7, 0, 0x02, 4, 0, 0, 0, 0, 0};
    deliver(QCOM_QRTR_DATA, port, LOCAL0, m, sizeof m);
}
static int sent(uint8_t service, uint16_t msg)
{
    for (int i = (int)out.n - 1; i >= 0; --i) if (out.service[i] == service && out.msg[i] == msg) return i;
    return -1;
}

static const uint32_t PORTS[] = {40, 41, 42, 43, 44, 45};  /* DMS NAS WDS WDA DPM UIM */
#define DPM_PORT 44u

static void start(void)
{
    static const CellConfig cfg = {0, 1, 1, 0, 0, 1, 1, 2, 18, 0xff, 0};
    static const CellSessionOps ops = {session_send, session_event, 0};
    memset(&out, 0, sizeof out);
    memset(&svcs, 0, sizeof svcs);
    qcom_qrtr_init(&q, 1, link_send, 0);
    assert(!qcom_qrtr_add_service(&q, 0, 0, LOCAL0, receive, 0));
    assert(!qcom_qrtr_add_service(&q, 0, 0, LOCAL0 + 1, receive, 0));
    cell_session_init(&s, &cfg, &ops, 0);
    ready_now = 1;
    for (unsigned i = 0; i < DATA_SERVICE_COUNT; ++i) server(QCOM_QRTR_NEW_SERVER, data_service_ids[i], PORTS[i], 1);
    assert(s.state == CS_DPM && sent(QMI_SVC_DPM, DPM_OPEN_PORT) >= 0);
}

/* nothing but Stop Network may leave after the loss */
static void only_cleanup_after(unsigned mark)
{
    for (unsigned i = mark; i < out.n; ++i) assert(out.service[i] == QMI_SVC_WDS && out.msg[i] == WDS_STOP_NETWORK);
}

int main(void)
{
    for (int hook = 1; hook >= 0; --hook) {
        /* DEL_SERVER(DPM), then its already queued success, in one batch: the reply must not advance to WDA */
        start();
        const uint16_t txn = out.txn[sent(QMI_SVC_DPM, DPM_OPEN_PORT)];
        unsigned mark = out.n;
        server(QCOM_QRTR_DEL_SERVER, QMI_SVC_DPM, DPM_PORT, hook);
        reply(DPM_PORT, DPM_OPEN_PORT, txn);
        cell_session_poll(&s, 100);
        assert(s.state >= CS_STOPPING && sent(QMI_SVC_WDA, WDA_SET_DATA_FORMAT) < 0);
        only_cleanup_after(mark);
    }
    /* DEL then NEW at the same endpoint in one batch, then the late success: the loss is still seen (needs the per-packet hook) */
    start();
    const uint16_t txn = out.txn[sent(QMI_SVC_DPM, DPM_OPEN_PORT)];
    const unsigned mark = out.n;
    server(QCOM_QRTR_DEL_SERVER, QMI_SVC_DPM, DPM_PORT, 1);
    server(QCOM_QRTR_NEW_SERVER, QMI_SVC_DPM, DPM_PORT, 1);
    reply(DPM_PORT, DPM_OPEN_PORT, txn);
    cell_session_poll(&s, 100);
    assert(s.state >= CS_STOPPING && sent(QMI_SVC_WDA, WDA_SET_DATA_FORMAT) < 0);
    only_cleanup_after(mark);
    /* with every service still there, the same success does advance: the test is not passing by refusing everything */
    start();
    reply(DPM_PORT, DPM_OPEN_PORT, out.txn[sent(QMI_SVC_DPM, DPM_OPEN_PORT)]);
    assert(s.state == CS_WDA && sent(QMI_SVC_WDA, WDA_SET_DATA_FORMAT) >= 0);
    /* a service that moves (DEL at the old port, NEW at another) counts as lost; replies from the old port are no longer routed */
    start();
    server(QCOM_QRTR_DEL_SERVER, QMI_SVC_DPM, DPM_PORT, 1);
    server(QCOM_QRTR_NEW_SERVER, QMI_SVC_DPM, 99, 1);
    assert(s.state >= CS_STOPPING && data_services_lookup(&svcs, MODEM, DPM_PORT) < 0 && data_services_lookup(&svcs, MODEM, 99) == QMI_SVC_DPM);
    /* readiness lost just before the last missing service (DPM) is announced: nothing is sent and no setup begins */
    for (int via_receive = 0; via_receive < 2; ++via_receive) {
        static const CellConfig cfg = {0, 1, 1, 0, 0, 1, 1, 2, 18, 0xff, 0};
        static const CellSessionOps ops = {session_send, session_event, 0};
        memset(&out, 0, sizeof out);
        memset(&svcs, 0, sizeof svcs);
        qcom_qrtr_init(&q, 1, link_send, 0);
        assert(!qcom_qrtr_add_service(&q, 0, 0, LOCAL0, receive, 0));
        cell_session_init(&s, &cfg, &ops, 0);
        ready_now = 1;
        for (unsigned i = 0; i < DATA_SERVICE_COUNT; ++i)
            if (data_service_ids[i] != QMI_SVC_DPM) server(QCOM_QRTR_NEW_SERVER, data_service_ids[i], PORTS[i], 1);
        assert(s.state == CS_WAIT_READY && s.is_ready && svcs.ready_given);  /* granted while DPM was still missing */
        const unsigned mark = out.n;
        ready_now = 0;  /* the owner sees the IPA (or USIM) fail ... */
        server(QCOM_QRTR_NEW_SERVER, QMI_SVC_DPM, DPM_PORT, !via_receive);  /* ... and in the same step DPM appears */
        if (via_receive) reply(PORTS[0], DMS_GET_REVISION, 1);  /* the next routed packet runs the step instead of the hook */
        cell_session_poll(&s, 100);
        assert(sent(QMI_SVC_DPM, DPM_OPEN_PORT) < 0 && s.state == CS_WAIT_READY && !s.is_ready && !svcs.ready_given);
        for (unsigned i = mark; i < out.n; ++i) assert(out.msg[i] != DPM_OPEN_PORT && out.msg[i] != NAS_REGISTER_INDICATIONS);
        /* readiness back: setup starts normally */
        ready_now = 1;
        discovery();
        assert(s.state == CS_DPM && sent(QMI_SVC_DPM, DPM_OPEN_PORT) >= 0);
    }
    puts("data service cache checks: PASS");
    return 0;
}
