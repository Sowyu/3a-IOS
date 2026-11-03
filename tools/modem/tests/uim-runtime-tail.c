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
}
