from pathlib import Path
here=Path(__file__).resolve().parent;d=here/'tests/out'
s=(d/'ModemRuntime.inc').read_text();tests=here/'tests'
prefix=(tests/'uim-runtime-prefix.c').read_text()
prefix+='\n#include "QcomGlink.h"\nstatic uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}\n'
code=s[s.index('static void m_uim_readiness('):s.index('static int m_glink_prepare(')]
tail=(tests/'uim-runtime-tail.c').read_text().replace('int main(void){','static int original_tests(void){')
a=tail.rfind('}');tail=tail[:a]+'return 0;\n'+tail[a:]
extra=r'''
static unsigned callbacks;
static void data_receive(void *ctx,QcomQrtr *q,uint32_t p,uint32_t n,uint32_t f,const uint8_t *d,uint32_t l){
 (void)ctx;(void)q;(void)p;(void)n;(void)f;(void)d;(void)l;assert(!data_ready && !m.uim_ready);callbacks++;
}
static void word(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static void start_ready(void){reset_client();m.uim_node=9;m.uim_port=100;m.data_attached=true;data_ready=false;notices=callbacks=0;m_uim_readiness(true);assert(data_ready && notices==1);assert(!qcom_qrtr_add_service(&m.qr,0,0,0x4010,data_receive,0));}
int main(void){
 original_tests();uint8_t packet[52]={0};word(packet,1);word(packet+8,9);word(packet+12,101);word(packet+20,1);word(packet+24,1);word(packet+28,0x4010);word(packet+4,QCOM_QRTR_DATA);
 start_ready();now=15000;m_greceive(0,0,packet,33);assert(callbacks==1 && notices==2);
 start_ready();m.fatal=true;m_greceive(0,0,packet,33);assert(callbacks==1 && notices==2);
 start_ready();m_gstate(0,0,0);assert(!data_ready && m.failed);m_greceive(0,0,packet,33);assert(callbacks==1);
 start_ready();uint8_t del[52]={0};word(del,1);word(del+4,QCOM_QRTR_DEL_SERVER);word(del+8,9);word(del+12,QCOM_QRTR_PORT_CTRL);word(del+20,20);word(del+24,1);word(del+28,QCOM_QRTR_PORT_CTRL);word(del+32,QCOM_QRTR_DEL_SERVER);word(del+36,11);word(del+44,9);word(del+48,100);
 m_greceive(0,0,del,52);assert(!data_ready && !m.uim_ready && m.failed && notices==2);m_greceive(0,0,packet,33);assert(callbacks==1);
 start_ready();m.data_stopping=true;m_uim_readiness(false);m_uim_readiness(true);assert(!data_ready && !m.uim_ready && notices==2);
 puts("PASS: actual QRTR dispatch revokes data readiness before subsequent callbacks on expiry, fatal, GLINK loss or UIM removal; teardown cannot re-enable it");return 0;
}
'''
(d/'readiness.c').write_text(prefix+code+tail+extra)
