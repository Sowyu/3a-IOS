#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "QcomQrtr.h"
static unsigned sent, received;
static int send_packet(void *c,const uint8_t *p,uint32_t n){(void)c;assert(n>=32 && p[0]==1);sent++;return 0;}
static void receive(void *ctx,QcomQrtr *q,uint32_t port,uint32_t node,uint32_t from,const uint8_t *data,uint32_t len){
 (void)ctx;(void)q;assert(port==0x4002 && node==0 && from==9 && len==1 && data[0]==0x42);received++;
}
static void w(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
int main(void){
 QcomQrtr q;qcom_qrtr_init(&q,1,send_packet,0);qcom_qrtr_start(&q);assert(sent==1);
 assert(qcom_qrtr_send_data(&q,7,0,8,(const uint8_t *)"x",UINT32_MAX));assert(sent==1);
 uint8_t p[52]={0};w(p,1);w(p+4,QCOM_QRTR_NEW_SERVER);w(p+8,0);w(p+12,QCOM_QRTR_PORT_CTRL);w(p+20,20);w(p+24,1);w(p+28,QCOM_QRTR_PORT_CTRL);
 w(p+32,QCOM_QRTR_NEW_SERVER);w(p+36,0x0b);w(p+40,1);w(p+44,0);w(p+48,9);
 assert(!qcom_qrtr_receive(&q,p,sizeof(p)));int i=qcom_qrtr_find_remote(&q,0x0b);assert(i>=0 && q.remote[i].port==9);
 w(p+20,UINT32_MAX);assert(qcom_qrtr_receive(&q,p,sizeof(p)));w(p+20,20);
 w(p+32,QCOM_QRTR_HELLO);assert(qcom_qrtr_receive(&q,p,sizeof(p)));w(p+32,QCOM_QRTR_DEL_SERVER);w(p+4,QCOM_QRTR_DEL_SERVER);
 assert(!qcom_qrtr_receive(&q,p,sizeof(p)));assert(qcom_qrtr_find_remote(&q,0x0b)<0);
 uint8_t m[]={2,1,0,0x2f,0,7,0,2,4,0,0,0,0,0};QcomQmiMessage msg;
 assert(!qcom_qmi_parse(m,sizeof(m),&msg));uint32_t n;assert(qcom_qmi_tlv(&msg,2,&n) && n==4);
 for(unsigned j=0;j<sizeof(m);j++)assert(qcom_qmi_parse(m,j,&msg));
 m[5]=6;assert(qcom_qmi_parse(m,sizeof(m),&msg));m[5]=7;m[8]=255;assert(qcom_qmi_parse(m,sizeof(m),&msg));
 assert(!qcom_qrtr_add_service(&q,0,0,0x4002,receive,0));
 w(p+4,QCOM_QRTR_HELLO);w(p+32,QCOM_QRTR_HELLO);unsigned before=sent;assert(!qcom_qrtr_receive(&q,p,sizeof(p)));assert(sent==before);
 w(p+4,QCOM_QRTR_DATA);w(p+12,9);w(p+20,1);w(p+28,0x4002);p[32]=0x42;
 assert(!qcom_qrtr_receive(&q,p,33) && received==1);
 puts("PASS: QRTR service discovery/removal, length/command rejection, QMI TLV bounds");
}
