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
static void backpressure(unsigned reuse){
 ah=at=bh=bt=0;received=0;
 QcomGlinkFifo af={&ah,&at,ab,sizeof(ab)},bf={&bh,&bt,ba,sizeof(ba)};
 qcom_glink_init(&a,af,bf,0,0);qcom_glink_init(&b,bf,af,0,0);
 a.features=b.features=reuse?QCOM_GLINK_FEATURE_INTENT_REUSE:0;
 QcomGlinkClient client={0,receive};
 QcomGlinkChannel *ac=qcom_glink_channel(&a,"IPCRTR",2048,1,astorage,&client,0,1);
 QcomGlinkChannel *bc=qcom_glink_channel(&b,"IPCRTR",2048,1,bstorage,&client,0,1);
 for(unsigned i=0;i<100;i++){qcom_glink_poll(&a);qcom_glink_poll(&b);}
 assert(ac->up && bc->up && bh==bt);
 /* Occupy all usable B->A space with harmless READ_NOTIF commands. */
 for(unsigned i=0;i<sizeof(ba)-24;i+=8){for(unsigned j=0;j<8;j++)ba[(bh+i+j)%sizeof(ba)]=j?0:QCOM_GLINK_READ_NOTIF;}
 bh=(bh+sizeof(ba)-24)%sizeof(ba);assert(!qcom_glink_tx_avail(&b.tx));
 assert(!qcom_glink_send(&a,ac,"abc",3));qcom_glink_poll(&b);
 assert(received==1 && bc->local[0].busy && ac->remote[0].busy);
 qcom_glink_poll(&b);assert(received==1 && bc->local[0].busy);
 qcom_glink_poll(&a);qcom_glink_poll(&b);qcom_glink_poll(&a);
 assert(!bc->local[0].busy && !ac->remote[0].busy && ac->remote[0].valid);
 assert(!qcom_glink_send(&a,ac,"abc",3));
 for(unsigned i=0;i<10;i++){qcom_glink_poll(&b);qcom_glink_poll(&a);}
 assert(received==2 && !a.broken && !b.broken);
}
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
 backpressure(0);backpressure(1);
 /* Impossible fragment size must break the stream, not wait forever. */
 qcom_glink_init(&a,af,bf,0,0);bh=16;bt=0;memset(ba,0,sizeof(ba));ba[0]=QCOM_GLINK_TX_DATA;w(ba+8,UINT32_MAX);
 qcom_glink_poll(&a);assert(a.broken);
 qcom_glink_init(&a,af,bf,0,0);bh=16;bt=0;memset(ba,0,sizeof(ba));ba[0]=QCOM_GLINK_TX_DATA;w(ba+8,sizeof(ba)-16);
 qcom_glink_poll(&a);assert(a.broken);
 puts("PASS: GLINK handshake, backpressure completion/reuse exactly once, TX/RX bounds");
}
