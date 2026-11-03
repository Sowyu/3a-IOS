#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "QcomGlink.h"
static uint32_t ah,at,bh,bt;
static uint8_t ab[4096],ba[4096],astorage[8192],bstorage[8192];
static QcomGlink a,b;
static int received;
static void receive(void *ctx,QcomGlinkChannel *ch,const uint8_t *data,uint32_t length){
 (void)ctx;(void)ch;assert(length==3 && !memcmp(data,"abc",3));received++;
}
static void w(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
int main(void){
 QcomGlinkFifo af={&ah,&at,ab,sizeof(ab)},bf={&bh,&bt,ba,sizeof(ba)};
 qcom_glink_init(&a,af,bf,0,0);qcom_glink_init(&b,bf,af,0,0);
 QcomGlinkClient client={0,receive};
 QcomGlinkChannel *ac=qcom_glink_channel(&a,"IPCRTR",2048,4,astorage,&client,0,1);
 QcomGlinkChannel *bc=qcom_glink_channel(&b,"IPCRTR",2048,4,bstorage,&client,0,1);
 assert(ac && bc);
 for(unsigned i=0;i<100;i++){qcom_glink_poll(&a);qcom_glink_poll(&b);}
 assert(ac->up && bc->up && !a.broken && !b.broken);
 assert(qcom_glink_send(&a,ac,"x",UINT32_MAX)==QcomGlinkTooBig);
 assert(!qcom_glink_send(&a,ac,"abc",3));
 for(unsigned i=0;i<100;i++){qcom_glink_poll(&a);qcom_glink_poll(&b);}
 assert(received==1);
 /* Impossible fragment size must break the stream, not wait forever. */
 qcom_glink_init(&a,af,bf,0,0);bh=16;bt=0;memset(ba,0,sizeof(ba));ba[0]=QCOM_GLINK_TX_DATA;w(ba+8,UINT32_MAX);
 qcom_glink_poll(&a);assert(a.broken);
 qcom_glink_init(&a,af,bf,0,0);bh=16;bt=0;memset(ba,0,sizeof(ba));ba[0]=QCOM_GLINK_TX_DATA;w(ba+8,sizeof(ba)-16);
 qcom_glink_poll(&a);assert(a.broken);
 puts("PASS: GLINK handshake/IPCRTR delivery, TX overflow rejection, impossible RX lengths");
}
