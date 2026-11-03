#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "QcomQrtr.h"
static unsigned received,accepted,attempts,pending;static int blocked,model;
static uint8_t last[64];
static uint32_t rd(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static int send(void *ctx,const uint8_t *p,uint32_t n){
 (void)ctx;attempts++;assert(n==52 && rd(p)==1 && rd(p+4)==QCOM_QRTR_RESUME_TX && !rd(p+16));
 if(blocked)return -1;
 assert(rd(p+8)==1 && rd(p+12)==0x4002 && rd(p+24)==9);
 assert(rd(p+20)==20 && rd(p+32)==QCOM_QRTR_RESUME_TX && rd(p+36)==1 && rd(p+40)==0x4002 && !rd(p+44) && !rd(p+48));
 if(model){assert(rd(p+28)==100);pending=0;}
 memcpy(last,p,n);accepted++;return 0;
}
static void receive(void *ctx,QcomQrtr *q,uint32_t port,uint32_t node,uint32_t from,const uint8_t *p,uint32_t n){
 (void)ctx;(void)q;(void)from;assert(port==0x4002 && node==9 && n==1 && *p==0x42);received++;
}
static void packet(uint8_t *p,uint32_t from,int confirm){
 memset(p,0,52);wr(p,1);wr(p+4,QCOM_QRTR_DATA);wr(p+8,9);wr(p+12,from);wr(p+16,confirm);wr(p+20,1);wr(p+24,1);wr(p+28,0x4002);p[32]=0x42;
}
static void init(QcomQrtr *q){qcom_qrtr_init(q,1,send,0);assert(!qcom_qrtr_add_service(q,0,0,0x4002,receive,0));received=accepted=attempts=pending=0;blocked=model=0;}
int main(void){
 QcomQrtr q;uint8_t p[52];init(&q);model=1;
 for(unsigned i=0;i<60;i++){assert(pending<10);pending++;packet(p,100,pending==5);assert(!qcom_qrtr_receive(&q,p,33));}
 assert(received==60 && accepted==12 && q.resumes_sent==12 && !q.resume_count);
 init(&q);blocked=1;packet(p,100,1);assert(!qcom_qrtr_receive(&q,p,33));assert(received==1 && q.resume_count==1 && !q.resumes_sent);
 assert(!qcom_qrtr_poll(&q) && q.resume_count==1);assert(received==1);
 assert(!qcom_qrtr_receive(&q,p,33) && q.resume_count==1 && received==2); /* same flow coalesces */
 blocked=0;assert(!qcom_qrtr_poll(&q) && !q.resume_count && accepted==1 && received==2);
 assert(rd(last+28)==100);assert(!qcom_qrtr_poll(&q) && accepted==1);
 init(&q);blocked=1;
 for(unsigned i=0;i<QCOM_QRTR_MAX_RESUMES;i++){packet(p,100+i,1);assert(!qcom_qrtr_receive(&q,p,33));}
 assert(q.resume_count==QCOM_QRTR_MAX_RESUMES);blocked=0;assert(!qcom_qrtr_poll(&q) && accepted==QCOM_QRTR_MAX_RESUMES && !q.resume_count);
 init(&q);blocked=1;
 for(unsigned i=0;i<QCOM_QRTR_MAX_RESUMES;i++){packet(p,100+i,1);assert(!qcom_qrtr_receive(&q,p,33));}
 packet(p,200,1);assert(qcom_qrtr_receive(&q,p,33)==-1 && q.resume_failed && qcom_qrtr_poll(&q)==-1);
 init(&q);packet(p,100,0);assert(!qcom_qrtr_receive(&q,p,33) && received==1 && !attempts);
 packet(p,100,1);wr(p+24,2);assert(qcom_qrtr_receive(&q,p,33)==-1 && received==1 && !attempts);
 packet(p,100,1);wr(p+20,2);assert(qcom_qrtr_receive(&q,p,33)==-1 && received==1 && !attempts);
 puts("PASS: 60-message peer window, reversed endpoints/local flow key, backpressure/coalescing, full queue recovery and overflow failure");return 0;
}
