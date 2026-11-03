int main(void){
 assert(!modem_tftp_probe_allowed(""));
 const uint8_t rrq[]={0,1,'a',0,'o','c','t','e','t',0};
 QcomQrtr q={0};
 m_tftp_probe_receive(0,&q,0x4006,3,42,rrq,sizeof(rrq));assert(!sends && !m.tftp_requests);
 m.tftp_probe=true;
 for(unsigned i=0;i<64;i++)m_tftp_probe_receive(0,&q,0x4006,3,42,rrq,sizeof(rrq));
 assert(sends==64 && m.tftp_requests==64 && !m.failed);
 m_tftp_probe_receive(0,&q,0x4006,3,42,rrq,sizeof(rrq));assert(m.failed && sends==64);
 m.failed=false;m.tftp_requests=0;fail_send=1;
 m_tftp_probe_receive(0,&q,0x4006,3,42,rrq,sizeof(rrq));assert(m.failed && sends==65);
 puts("PASS actual callback: disabled inert, 64 reply cap, failed send terminates");
}
