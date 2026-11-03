#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ModemUim.h"
#include "QcomQrtr.h"
static struct {bool uim,qr_started,failed;uint32_t uim_node,uim_port,uim_done,uim_tries[2];uint16_t uim_txn[2];uint64_t uim_next;QcomQrtr qr;}m;
static uint64_t now;
static unsigned sent;
static uint16_t le16(const uint8_t *p){return p[0]|p[1]<<8;}
static uint64_t m_now(void){return now;}
static void sweet_log_sync(void){}
static int send_packet(void *ctx,const uint8_t *p,uint32_t n){
 (void)ctx;assert(n==40 && p[4]==QCOM_QRTR_DATA && p[32]==0 && (p[35]==0x2f || p[35]==0x47));
 assert(!p[36] && !p[37] && !p[38]);sent++;return 0;
}
