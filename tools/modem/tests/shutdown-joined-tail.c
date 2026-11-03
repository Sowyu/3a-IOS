static uint8_t shadow[1024],window[1024];
static unsigned ipc_mode,ipc_polls,packets;
static uint16_t le16(const uint8_t *p){return p[0]|p[1]<<8;}
static void w32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static int wire_send(void *ctx,const uint8_t *p,uint32_t n){
 (void)ctx;assert(n>=39 && p[4]==QCOM_QRTR_DATA);
 if(p[12]==3){const uint8_t req[7]={0,1,0,0x21,0,0,0};assert(n==40 && p[13]==0x40 && !memcmp(p+32,req,7));}
 else assert(p[12]==0 && p[13]==0x40 && p[32]==QCOM_QMI_RESPONSE && !p[42]);
 packets++;return 0;
}
static void receive(uint32_t port,const uint8_t *data,uint32_t len){
 uint8_t packet[128]={0};w32(packet,1);w32(packet+4,QCOM_QRTR_DATA);w32(packet+8,0);w32(packet+12,4);
 w32(packet+20,len);w32(packet+24,1);w32(packet+28,port);memcpy(packet+32,data,len);
 assert(!qcom_qrtr_receive(&m.qr,packet,(32+len+3)&~3u));
}
static void m_ipc_poll(void){
 ipc_polls++;
 if(ipc_mode==1 && ipc_polls==1){
  uint8_t msg[34]={0};msg[3]=3;msg[5]=27;msg[7]=1;msg[8]=4;msg[14]=2;msg[15]=1;msg[18]=3;msg[19]=13;msg[21]=1;w32(msg+26,m.fs.window_pa);w32(msg+30,1);
  receive(0x4000,msg,sizeof(msg));assert(window[0]==0x55 && m.fs.reads==1);
  window[0]=0x77;msg[17]=1;receive(0x4000,msg,sizeof(msg));assert(shadow[0]==0x77 && m.fs.writes==1);
  const uint8_t response[14]={2,1,0,0x21,0,7,0,2,4,0,0,0,0,0};receive(0x4003,response,sizeof(response));
  const uint8_t ind[7]={4,0,0,0x21,0,0,0};receive(0x4003,ind,sizeof(ind));
 }
 if(ipc_mode==2)m.failed=true;
 if(ipc_mode==3)m.fatal=true;
}
static void joined_init(void){
 init();m.sysmon=m.qr_started=true;ipc_mode=ipc_polls=packets=0;
 memset(shadow,0x55,sizeof(shadow));memset(window,0xaa,sizeof(window));m.fs.image[0]=shadow;m.fs.bytes[0]=sizeof(shadow);m.fs.window=window;m.fs.window_bytes=sizeof(window);m.fs.window_pa=0xe1000000;m.fs.opened=1;
 qcom_qrtr_init(&m.qr,1,wire_send,0);
 assert(!qcom_qrtr_add_service(&m.qr,0,0,0x4003,m_shutdown_receive,0));
 assert(!qcom_qrtr_add_service(&m.qr,14,1,0x4000,m_rmtfs_receive,0));
 m.qr.remote_count=1;m.qr.remote[0].service=43;m.qr.remote[0].instance=0x1202;m.qr.remote[0].node=0;m.qr.remote[0].port=4;
}
int main(void){
 joined_init();ipc_mode=1;assert(m_cleanup());assert(shutdowns==1 && !stop_updates && m.fs.reads==1 && m.fs.writes==1 && reclaims==1 && packets==3);
 joined_init();assert(m_cleanup());assert(now==10000 && stop_updates==2 && shutdowns==1);
 joined_init();ipc_mode=1;shut_status=-3145800;assert(!m_cleanup());assert(shutdowns==50 && !stop_updates && !reclaims && !unmaps && !frees);
 joined_init();ipc_mode=2;assert(!m_cleanup());assert(shutdowns==1 && stop_updates==2 && reclaims==1 && unmaps==4);
 joined_init();ipc_mode=3;assert(!m_cleanup());assert(shutdowns==1 && stop_updates==2 && reclaims==1);
 puts("PASS: joined SSCTL/QRTR RAM read-write progress, fallback, PAS retention and shutdown failure result");
}
