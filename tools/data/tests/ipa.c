/* Host test for patch 2221's adapter (sweet_data_ipa.c) against stubs of both sides: 2220's data hooks and 2202's IPA API. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../modem/SweetIpa.h"
#include "sweet_data.h"
#include "sweet_data_ipa.h"

static struct {
    int set_rc, reg_rc, tx_rc, ready, sets, regs, rx_calls, readies, last_ready, qmap;
    uint32_t rx_bytes, datagrams, rx_len, tx_len;
    int (*tx)(void *, const uint8_t *, uint32_t);
    sweet_ipa_rx_fn rx;
    const void *tx_buf;
} st;

int sweet_data_set_ipa(int (*tx)(void *, const uint8_t *, uint32_t), void *ctx, int qmap, uint32_t rx_bytes, uint32_t datagrams)
{
    (void)ctx;
    st.sets++;
    if (st.set_rc) return st.set_rc;
    st.tx = tx; st.qmap = qmap; st.rx_bytes = rx_bytes; st.datagrams = datagrams;
    return 0;
}
void sweet_data_ipa_rx(const uint8_t *buf, uint32_t length) {(void)buf; st.rx_calls++; st.rx_len = length;}
void sweet_data_ipa_ready(int ready) {st.readies++; st.last_ready = ready;}
int sweet_ipa_transmit(const void *qmap, size_t bytes) {st.tx_buf = qmap; st.tx_len = (uint32_t)bytes; return st.tx_rc;}
int sweet_ipa_register_rx(sweet_ipa_rx_fn receive, void *context) {(void)context; st.regs++; if (!st.reg_rc) st.rx = receive; return st.reg_rc;}
bool sweet_ipa_ready(void) {return st.ready;}

int main(void)
{
    /* bind: TX hook with SweetIpa.h's limits, then RX */
    assert(sweet_data_ipa_bind() == 0 && st.sets == 1 && st.regs == 1 && st.tx && st.rx);
    assert(st.qmap == 1 && st.rx_bytes == 8192 && st.datagrams == 16);
    /* TX: the frame and its length reach sweet_ipa_transmit; 0 stays 0, every errno becomes -1 */
    uint8_t frame[64] = {0x00, 1, 0, 20};
    assert(st.tx(NULL, frame, 24) == 0 && st.tx_buf == frame && st.tx_len == 24);
    const int errs[] = {ENETDOWN, EAGAIN, EMSGSIZE, EINVAL};
    for (unsigned i = 0; i < 4; ++i) {st.tx_rc = errs[i]; assert(st.tx(NULL, frame, 24) == -1);}
    st.tx_rc = 0;
    /* RX: up to 8192 bytes pass, larger never reach the data side */
    static uint8_t agg[SWEET_IPA_RX_BYTES + 1];
    st.rx(NULL, agg, SWEET_IPA_RX_BYTES);
    assert(st.rx_calls == 1 && st.rx_len == SWEET_IPA_RX_BYTES);
    st.rx(NULL, agg, SWEET_IPA_RX_BYTES + 1);
    st.rx(NULL, agg, (size_t)1 << 33);  /* would wrap to 0 in 32 bits */
    assert(st.rx_calls == 1);
    /* refresh copies the IPA latch, both ways */
    st.ready = 1; sweet_data_ipa_refresh(); assert(st.last_ready == 1);
    st.ready = 0; sweet_data_ipa_refresh(); assert(st.last_ready == 0 && st.readies == 2);
    /* data side refuses (sweetdata off, attached, already bound): EPERM, and RX is never registered */
    memset(&st, 0, sizeof st);
    st.set_rc = -1;
    assert(sweet_data_ipa_bind() == EPERM && st.regs == 0 && !st.rx);
    /* another receiver owns RX: EBUSY after the TX hook went in, the partial bind the header makes terminal */
    memset(&st, 0, sizeof st);
    st.reg_rc = EBUSY;
    assert(sweet_data_ipa_bind() == EBUSY && st.tx && !st.rx && st.sets == 1 && st.regs == 1);
    puts("data IPA adapter checks: PASS");
    return 0;
}
