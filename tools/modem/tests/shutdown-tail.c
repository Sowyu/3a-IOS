static uint8_t response[14]={2,1,0,0x21,0,7,0,2,4,0,0,0,0,0};
static const uint8_t indication[7]={4,0,0,0x21,0,0,0};
static void m_ipc_poll(void){
 polls++;
 if(mode==1){m_shutdown_receive(0,&m.qr,0x4003,0,4,response,14);m.state|=0x80;}
 if(mode==2){m_shutdown_receive(0,&m.qr,0x4003,0,4,indication,7);m_shutdown_receive(0,&m.qr,0x4003,0,4,response,14);}
 if(mode==3)m_shutdown_receive(0,&m.qr,0x4003,0,4,response,14);
 if(mode==4)m_shutdown_receive(0,&m.qr,0x4003,0,4,indication,7);
 if(mode==5)m.fatal=true;
}
static void init(void){
 memset(&m,0,sizeof(m));now=sent=polls=mode=0;m.sysmon=m.started=m.qr_started=true;
 qcom_qrtr_init(&m.qr,1,send_packet,0);m.qr.remote_count=1;
 m.qr.remote[0].service=43;m.qr.remote[0].instance=0x1202;m.qr.remote[0].node=0;m.qr.remote[0].port=4;
}
int main(void){
 init();m.sysmon=false;assert(!m_shutdown_request() && !sent && !polls);
 init();m.started=false;assert(!m_shutdown_request() && !sent);
 init();m.qr_started=false;assert(!m_shutdown_request() && !sent);
 init();m.failed=true;assert(!m_shutdown_request() && !sent);
 init();m.qr.remote[0].instance=0x1402;assert(!m_shutdown_request() && !sent);
 init();m.qr.remote[0].service=11;assert(!m_shutdown_request() && !sent);
 init();m.state=0x80;assert(!m_shutdown_request() && !sent);
 init();mode=1;assert(m_shutdown_request() && sent==1 && polls==1);
 init();mode=2;m.qr.remote[0].instance=1;assert(m_shutdown_request() && sent==1);
 init();mode=3;assert(!m_shutdown_request() && sent==1 && now==10000);
 init();mode=4;assert(!m_shutdown_request() && sent==1 && now==10000);
 init();mode=5;assert(!m_shutdown_request() && polls==1);
 init();mode=6;assert(!m_shutdown_request() && !polls && !m.shutdown_sent);
 init();m.shutdown_port=4;m_shutdown_receive(0,&m.qr,0x4003,0,4,response,14);assert(!m.shutdown_response);
 m.shutdown_sent=true;m_shutdown_receive(0,&m.qr,0x4003,1,4,response,14);assert(!m.shutdown_response);
 m_shutdown_receive(0,&m.qr,0x4003,0,5,response,14);assert(!m.shutdown_response);
 for(unsigned n=0;n<14;n++){m_shutdown_receive(0,&m.qr,0x4003,0,4,response,n);assert(!m.shutdown_response);}
 for(unsigned i=0;i<14;i++){
  uint8_t bad[14];memcpy(bad,response,14);bad[i]^=0x80;m_shutdown_receive(0,&m.qr,0x4003,0,4,bad,14);assert(!m.shutdown_response);
 }
 uint8_t dup[21];memcpy(dup,response,14);memcpy(dup+14,response+7,7);dup[5]=14;
 m_shutdown_receive(0,&m.qr,0x4003,0,4,dup,21);assert(!m.shutdown_response);
 m_shutdown_receive(0,&m.qr,0x4003,0,4,response,14);assert(m.shutdown_response);
 puts("PASS: actual SSCTL sender/response bounds, endpoint binding, acknowledgement and timeout");
}
