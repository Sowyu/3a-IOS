#include <assert.h>
#include <stdio.h>
#include "IpaQmi.h"
static unsigned sends;static uint8_t last[512];static uint32_t lastlen;static int refuse;
static int send_packet(void *ctx,const uint8_t *p,uint32_t n){(void)ctx;if(refuse)return -1;sends++;assert(n<=sizeof(last));memcpy(last,p,n);lastlen=n;return 0;}
static void setup(QcomQrtr *qr,IpaQmi *s){
    qcom_qrtr_init(qr,1,send_packet,0);*s=(IpaQmi){.mem_size=0x4000,.smem_size=0xb000};qr->remote_count=1;
    qr->remote[0].service=0x31;qr->remote[0].instance=0x201;qr->remote[0].node=0;qr->remote[0].port=28;qr->hello_received=1;qr->remote_node=0;sends=0;refuse=0;
}
static void receive(IpaQmi *s,QcomQrtr *q,unsigned msg){
    uint8_t p[14]={0};p[0]=msg==0x21?2:0;ipa_put16(p+1,1);ipa_put16(p+3,(uint16_t)msg);
    if(msg==0x21){p[5]=7;p[7]=2;p[8]=4;}
    else{p[5]=4;p[7]=msg==0x20?0x10:1;p[8]=1;p[10]=1;}
    ipa_qmi_receive(s,q,msg==0x21?IPA_QMI_CLIENT:IPA_QMI_SERVER,0,msg==0x21?28:80,p,msg==0x21?14:11);
}
int main(void){
    QcomQrtr q;IpaQmi s;
    for(unsigned order=0;order<2;order++){
        setup(&q,&s);assert(!ipa_qmi_poll(&s,&q,0,false));assert(!sends);
        assert(!ipa_qmi_poll(&s,&q,0,true));assert(sends==1 && lastlen<512);
        receive(&s,&q,order?0x35:0x20);assert(!ipa_qmi_poll(&s,&q,1,true));
        receive(&s,&q,0x21);assert(!ipa_qmi_poll(&s,&q,2,true));
        receive(&s,&q,order?0x20:0x35);assert(ipa_qmi_poll(&s,&q,3,true)==1);
        unsigned previous=sends;assert(ipa_qmi_poll(&s,&q,4,true)==1 && sends==previous);
    }
    setup(&q,&s);receive(&s,&q,0x20);assert(s.registered && !s.bound);
    receive(&s,&q,0x35);assert(s.uc_done);assert(!ipa_qmi_poll(&s,&q,0,true));receive(&s,&q,0x21);assert(ipa_qmi_poll(&s,&q,1,true)==1);
    setup(&q,&s);receive(&s,&q,0x21);assert(!s.accepted);
    assert(!ipa_qmi_poll(&s,&q,100,true));assert(ipa_qmi_poll(&s,&q,60100,true)==-1);
    setup(&q,&s);assert(!ipa_qmi_poll(&s,&q,1,true));q.remote[0].port++;assert(ipa_qmi_poll(&s,&q,2,true)==-1);
    setup(&q,&s);refuse=1;assert(ipa_qmi_poll(&s,&q,1,true)==-1);
    uint8_t request[256];assert(!ipa_qmi_init_request(request,1,0xffff,0x4000,0xb000));assert(!ipa_qmi_init_request(request,1,0,0x3fe7,0xb000));
    /* Independent golden wire bytes from Nothing's qmi_elem_info schema.
     * A nonzero SRAM prefix must not leak into filter-stat offset0x1cd0. */
    static const uint8_t stats[]={0x23,10,0,0xd0,0x1c,0,0,0x40,3,0,0,128,179};
    static const uint8_t smem[]={0x24,8,0,0,0,0,0,0,0xb0,0,0};
    unsigned length=ipa_qmi_init_request(request,1,0x4000,0x4000,0xb000),at=7,seen=0;
    assert(length>24 && length<=sizeof(request));
    while(at<length){unsigned size=request[at+1]+256u*request[at+2];assert(at+3+size<=length);
        if(request[at]==0x23){assert(size==10 && !memcmp(request+at,stats,sizeof(stats)));seen|=1;}
        if(request[at]==0x24){assert(size==8 && !memcmp(request+at,smem,sizeof(smem)));seen|=2;}
        at+=3+size;
    }
    assert(at==length && seen==3);
    assert(0x1cd0+(179+1)*16<=0x2870); /* Entire indexed range fits FNR SRAM. */
    assert(!ipa_qmi_init_request(request,1,0,0x4000,0));
    assert(!ipa_qmi_init_request(request,1,0,0x4000,0xc000));
    setup(&q,&s);assert(!ipa_qmi_poll(&s,&q,0,true));
    uint8_t dup[21]={2,1,0,0x21,0,14,0,2,4,0,0,0,0,0,2,4,0,0,0,0,0};
    ipa_qmi_receive(&s,&q,IPA_QMI_CLIENT,0,28,dup,sizeof(dup));assert(!s.accepted);
    receive(&s,&q,0x21);assert(!s.accepted && s.failed);
    setup(&q,&s);assert(!ipa_qmi_poll(&s,&q,0,true));q.remote_count=0;receive(&s,&q,0x21);assert(!s.accepted && s.failed);
    setup(&q,&s);assert(!ipa_qmi_poll(&s,&q,0,true));
    uint8_t short_reply[5]={2,1,0,0x21,0};ipa_qmi_receive(&s,&q,IPA_QMI_CLIENT,0,28,short_reply,5);
    receive(&s,&q,0x21);assert(!s.accepted && s.failed);
    puts("IPA bidirectional QMI handshake, timeout and peer checks PASS");
}
