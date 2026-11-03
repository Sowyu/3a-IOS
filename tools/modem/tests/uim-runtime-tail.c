int main(void){
 uint8_t reply[32]={2,1,0,0x2f,0,25,0,2,4,0,0,0,0,0,0x10,15,0};reply[25]=1;
 m.uim_node=9;m.uim_port=100;m.uim_txn[0]=1;
 m_uim_receive(0,&m.qr,0x4002,9,100,reply,sizeof(reply));assert(!m.uim_done);
 qcom_qrtr_init(&m.qr,1,send_packet,0);m.qr.remote_count=1;m.qr.remote[0].service=11;m.qr.remote[0].node=9;m.qr.remote[0].port=100;
 m_uim_poll();assert(!sent);m.uim=m.qr_started=true;m_uim_poll();assert(sent==2 && m.uim_txn[0]==1 && m.uim_txn[1]==2);
 m_uim_poll();assert(sent==2);
 m_uim_receive(0,&m.qr,0x4002,8,100,reply,sizeof(reply));assert(!m.uim_done);
 reply[1]=7;m_uim_receive(0,&m.qr,0x4002,9,100,reply,sizeof(reply));assert(!m.uim_done);
 reply[1]=1;m_uim_receive(0,&m.qr,0x4002,9,100,reply,sizeof(reply));assert(m.uim_done==1);
 now=10000;m_uim_poll();assert(sent==3 && m.uim_txn[1]==4 && m.uim_tries[0]==1);
 now=20000;m_uim_poll();assert(sent==4 && m.uim_txn[1]==6);
 now=30000;m_uim_poll();assert(sent==4);
 now=40000;m.qr.remote[0].port=101;m_uim_poll();assert(m.failed && sent==4);
 puts("PASS: actual UIM sender gate, two read-only opcodes, endpoint/transaction matching and bounded retries");
 memset(&m,0,sizeof(m));sent=0;now=0;m.uim=m.qr_started=m.uim_ready_poll=true;
 qcom_qrtr_init(&m.qr,1,send_packet,0);m.qr.remote_count=1;m.qr.remote[0].service=11;m.qr.remote[0].node=9;m.qr.remote[0].port=100;
 m_uim_poll();assert(sent==2 && m.uim_pending[0]);
 uint8_t ready[46]={2,1,0,0x2f,0,39,0,2,4,0,0,0,0,0,0x10,29,0};
 ready[25]=1;ready[26]=1;ready[31]=1;ready[32]=2;ready[33]=7;
 m_uim_receive(0,&m.qr,0x4002,9,100,ready,sizeof(ready));assert(m.uim_ready && !m.uim_pending[0]);m.uim_done=3;
 now=5000;m_uim_poll();assert(sent==3 && m.uim_txn[0]==3);
 ready[33]=1;m_uim_receive(0,&m.qr,0x4002,9,100,ready,sizeof(ready));assert(m.uim_ready); /* stale transaction */
 ready[1]=3;m_uim_receive(0,&m.qr,0x4002,9,100,ready,sizeof(ready));assert(!m.uim_ready);
 ready[33]=7;m_uim_receive(0,&m.qr,0x4002,9,100,ready,sizeof(ready));assert(!m.uim_ready); /* duplicate consumed reply */
 now=10000;m_uim_poll();ready[1]=5;m_uim_receive(0,&m.qr,0x4002,9,100,ready,sizeof(ready));assert(m.uim_ready);
 now=25000;m_uim_poll();assert(!m.uim_ready);
 for(unsigned i=0;i<200;i++){now+=5000;m_uim_poll();}assert(m.uim_tries[0]==180 && m.uim_tries[1]==1);
 m.uim_ready=true;m.qr.remote_count=0;now+=5000;m_uim_poll();assert(!m.uim_ready && m.failed);
 puts("PASS: gated status refresh, selected USIM readiness, stale/duplicate suppression, expiry, service removal and request cap");

 for(unsigned which=0;which<2;which++)for(unsigned bad=0;bad<4;bad++){
  reset_client();m_uim_poll();m.uim_ready=true;
  uint8_t good[46]={2,1,0,0x2f,0,39,0,2,4,0,0,0,0,0,0x10,29,0};
  good[25]=1;good[26]=1;good[31]=1;good[32]=2;good[33]=7;unsigned bytes=sizeof(good);
  if(which){good[1]=2;good[3]=0x47;good[5]=11;good[15]=1;bytes=18;}
  uint8_t rejected[46];memcpy(rejected,good,sizeof(good));unsigned length=bytes;
  if(bad==0){rejected[5]=7;rejected[10]=1;rejected[12]=5;length=14;}
  if(bad==1)length=5;
  if(bad==2)length=6;
  if(bad==3)rejected[15]=255;
  m_uim_receive(0,&m.qr,0x4002,9,100,rejected,length);assert(!m.uim_pending[which] && !(m.uim_done&(1u<<which)));
  assert(!m.uim_ready);
  m_uim_receive(0,&m.qr,0x4002,9,100,good,bytes);assert(!(m.uim_done&(1u<<which)));
  assert(!m.uim_ready);
  now=5000;m_uim_poll();good[1]=which?4:3;m_uim_receive(0,&m.qr,0x4002,9,100,good,bytes);assert(m.uim_done&(1u<<which));
 }
 for(unsigned change=0;change<2;change++)for(unsigned receive_first=0;receive_first<2;receive_first++){
  reset_client();m_uim_poll();m.uim_ready=true;now=1;
  if(change)m.qr.remote[0].port=101;else m.qr.remote_count=0;
  if(!receive_first)m_uim_poll();
  uint8_t good[46]={2,1,0,0x2f,0,39,0,2,4,0,0,0,0,0,0x10,29,0};good[25]=1;good[26]=1;good[31]=1;good[32]=2;good[33]=7;
  m_uim_receive(0,&m.qr,0x4002,9,100,good,sizeof(good));
  assert(m.failed && !m.uim_ready && !m.uim_pending[0] && !m.uim_pending[1] && !m.uim_done);
 }
 puts("PASS: rejected and truncated transactions retire once; discovery loss revokes readiness before cadence and late reply admission");
}
