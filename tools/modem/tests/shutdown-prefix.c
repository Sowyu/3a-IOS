#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "QcomQrtr.h"
static struct {bool sysmon,started,qr_started,fatal,failed,shutdown_sent,shutdown_response,shutdown_ind;uint32_t shutdown_node,shutdown_port,state;QcomQrtr qr;}m;
static uint64_t now;
static unsigned sent,polls,mode;
static uint16_t le16(const uint8_t *p){return p[0]|p[1]<<8;}
static uint64_t m_now(void){return now;}
static void m_sleep(unsigned ms){now+=ms;}
static void m_smp_poll(void){}
static void m_ipc_poll(void);
static void sweet_log_sync(void){}
static int send_packet(void *ctx,const uint8_t *p,uint32_t n){
 (void)ctx;assert(n==40 && p[4]==QCOM_QRTR_DATA && p[8]==1 && p[12]==3 && p[13]==0x40);
 assert(p[24]==0 && p[28]==4);const uint8_t req[7]={0,1,0,0x21,0,0,0};assert(!memcmp(p+32,req,7));sent++;return mode==6?-1:0;
}
