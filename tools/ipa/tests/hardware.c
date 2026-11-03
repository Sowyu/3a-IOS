#define IPA_HOST_TEST 1
/* Compile the real hardware/table driver against a bounded GSI+SRAM model. */
#define main ipa_gsi_test_main
#include "gsi.c"
#undef main
#include <errno.h>
#define PAGE_SIZE 16384u
#define IPA_DMA_IOVA 0x20000000u
typedef uintptr_t vm_offset_t;
static struct {vm_offset_t dma,block;bool clock_pinned,retained;uint32_t smem_bytes;} ipa;
static uint32_t cfg[0x10000/4];static uint8_t local[0x10000];
static unsigned cfg_writes,cfg_drop,immediates,fail_immediate,cmd_seen;
static bool corrupt_read,hold_aggregate,fault_on_producer;
static uint32_t m_rd(vm_offset_t p,unsigned o){assert(p==ipa.block);if(o>=0x140000){assert(o<0x150000);return cfg[(o-0x140000)/4];}assert(o>=0x4000);if(fault_on_producer && o==0x4000+ipa_gev(3,4)){regs[0x25240/4]=0x02301000;fault_on_producer=false;}return rd(0,o-0x4000);}
static uint8_t *dma_ptr(uint64_t address,unsigned n){assert(address>=IPA_DMA_IOVA && address-IPA_DMA_IOVA<=sizeof(dma) && n<=sizeof(dma)-(address-IPA_DMA_IOVA));return dma+(address-IPA_DMA_IOVA);}
static void local_copy(uint64_t addr,unsigned at,unsigned n,bool read){assert(at<=sizeof(local) && n<=sizeof(local)-at);if(read)memcpy(dma_ptr(addr,n),local+at,n);else memcpy(local+at,dma_ptr(addr,n),n);}
static void command_complete(void){
 unsigned ring=regs[ipa_gch(6,2)/4]-IPA_DMA_IOVA,slots=(regs[ipa_gch(6,1)/4]&0xffffff)/16;
 unsigned at=ring+(cmd_seen%slots)*16;IpaTre tre;memcpy(&tre,dma+at,16);cmd_seen++;immediates++;
 if(immediates==fail_immediate)return;
 uint8_t *p=dma_ptr(tre.address,24);assert((tre.flags>>16)==3);
 if(tre.length==0x13){uint16_t v[4];uint64_t address;memcpy(v,p,8);memcpy(&address,p+8,8);assert(!v[0] && v[3]<=1);local_copy(address,v[2],v[1],v[3]);if(v[3] && corrupt_read)dma_ptr(address,v[1])[0]^=1;}
 else if(tre.length==3 || tre.length==4 || tre.length==7 || tre.length==8){uint64_t a,flags,b;memcpy(&a,p,8);memcpy(&flags,p+8,8);memcpy(&b,p+16,8);local_copy(a,(flags>>12)&65535,flags&4095,false);local_copy(b,(flags>>40)&65535,(flags>>28)&4095,false);}
 else if(tre.length==9){uint64_t address;uint32_t flags;memcpy(&address,p,8);memcpy(&flags,p+8,4);local_copy(address,(flags>>12)&65535,flags&4095,false);}else assert(0);
 unsigned er=regs[ipa_gev(0,2)/4]-IPA_DMA_IOVA;
 IpaEvent ev={IPA_DMA_IOVA+at,0,0,2,0,0x22,6};memcpy(dma+er+((cmd_seen-1)%slots)*16,&ev,16);regs[ipa_gev(0,4)/4]=IPA_DMA_IOVA+er+(cmd_seen%slots)*16;
}
static void m_wr(vm_offset_t p,unsigned o,uint32_t v){
 assert(p==ipa.block);if(o>=0x140000){assert(o<0x150000);cfg_writes++;if(cfg_writes==cfg_drop)return;unsigned off=o-0x140000;if(off==0x6b0){if(!hold_aggregate)cfg[0x100/4]&=~v;return;}cfg[off/4]=v;return;}
 assert(o>=0x4000);wr(0,o-0x4000,v);if(o==0x4000+0x24000+6*8)command_complete();
}
static void sweet_dma_sync_va(const void *p,unsigned n){assert((uintptr_t)p>=(uintptr_t)dma);sync1(0,(unsigned)((uintptr_t)p-(uintptr_t)dma),n);}
static void delay(unsigned n){(void)n;}
static void sweet_log_sync(void){}
#include "IpaHardware.inc"
#define LCK_GRP_DECLARE(name,label) int name
#define LCK_MTX_DECLARE(name,group) int *name=group
static bool queue_locked;
static void lck_mtx_lock(int **lock){(void)lock;assert(!queue_locked);queue_locked=true;}
static void lck_mtx_unlock(int **lock){(void)lock;assert(queue_locked);queue_locked=false;}
#include "IpaData.inc"
#include "QcomSmp2p.h"
static bool ipa_full;
static int ipa_smp_index;
static struct {QcomSmp2p sp;QcomQrtr qr;bool qr_started,failed,fatal,data_attached,data_stopping,data_ipa_bound,uim_ready;unsigned qcount,uim_port;uint64_t uim_seen;} m;
static unsigned rings;static uint64_t now;
static uint64_t m_now(void){return now;}
static void m_ring(unsigned client,unsigned signal){assert(client==2 && signal==2);rings++;}
#include "IpaSession.inc"
static int qsend(void *ctx,const uint8_t *p,uint32_t n){(void)ctx;assert(p && n>=32);m.qcount++;return 0;}
static void qreceive(unsigned msg){
 uint8_t p[14]={0};p[0]=msg==0x21?2:0;ipa_put16(p+1,1);ipa_put16(p+3,msg);
 if(msg==0x21){p[5]=7;p[7]=2;p[8]=4;}else{p[5]=4;p[7]=msg==0x20?0x10:1;p[8]=1;p[10]=1;}
 ipa_qmi_receive(&iph.qmi,&m.qr,msg==0x21?IPA_QMI_CLIENT:IPA_QMI_SERVER,0,msg==0x21?28:80,p,msg==0x21?14:11);
}
#include "QcomGlink.h"
static bool data_ready;
static void sweet_data_ipa_refresh(void){data_ready=sweet_ipa_ready();}
static void m_uim_readiness(bool value){m.uim_ready=value;}
static bool m_uim_endpoint_valid(void){return true;}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void sweet_data_discovery(QcomQrtr *qr){assert(qr==&m.qr && data_ready==sweet_ipa_ready());if(iph.gsi.failed)assert(!data_ready && m.failed);}
#include "worker-functions.inc"
static unsigned data_callbacks;
static void data_reply(void *ctx,QcomQrtr *qr,uint32_t p,uint32_t node,uint32_t port,const uint8_t *buf,uint32_t size){
 (void)ctx;(void)qr;(void)p;(void)node;(void)port;(void)buf;(void)size;assert(!data_ready && !sweet_ipa_ready());data_callbacks++;
}
static void word(uint8_t *p,uint32_t v){ipa_put32(p,v);}
static unsigned received;
static void rx_callback(void *ctx,const uint8_t *p,size_t n){assert(ctx==&received && !queue_locked);assert(n==8 && p[4]==0x45);received++;assert(!sweet_ipa_transmit(p,n));}
static void hreset(void){reset();memset(cfg,0,sizeof(cfg));memset(local,0x6a,sizeof(local));ipa.smem_bytes=0xb000;ipa.dma=(uintptr_t)dma-PAGE_SIZE;ipa.block=1;cfg[0x40/4]=(0x800u<<16)|0x800;cfg[0]=0x0b0f0b1a; /* ast175 measured FLAVOR: TX0..10, RX11..25. */cfg_writes=cfg_drop=immediates=fail_immediate=cmd_seen=0;corrupt_read=hold_aggregate=fault_on_producer=false;}
int supplied_hardware_main(void){
 hreset();regs[0x25240/4]=0x83c7ffe1;assert(!ipa_hardware_setup());assert(error_inits==1 && !regs[0x25240/4]);assert(ipa_hardware_stop());
 /* Real ast175 registers must admit the actual command/LAN/WAN endpoints. */
 hreset();cfg[0x40/4]=0x880;assert(!ipa_hardware_setup());
 assert(iph.offset==0 && iph.bytes==0x4400 && immediates==7);
 assert(iph.gsi.ch[0].endpoint==9 && iph.gsi.ch[0].tx);
 assert(iph.gsi.ch[1].endpoint==11 && !iph.gsi.ch[1].tx);
 assert(iph.gsi.ch[2].endpoint==2 && iph.gsi.ch[2].tx);
 assert(iph.gsi.ch[3].endpoint==18 && !iph.gsi.ch[3].tx);
 assert(ipa_hardware_stop());
 const uint32_t bad_flavor[]={0,0xffffffff,24|(12u<<16),0x0b0f091a,0x0c0e0b1a,0x0b070b1a,0x090f0b1a,0x0b100b1a};
 for(unsigned i=0;i<sizeof(bad_flavor)/sizeof(bad_flavor[0]);i++){
  hreset();cfg[0]=bad_flavor[i];assert(ipa_hardware_setup()==ENOTSUP);assert(!cfg_writes && !writes);
 }

 hreset();assert(!ipa_hardware_setup());assert(immediates==7 && iph.ready);assert(cfg[0x38/4]==(11|(11u<<8)|(3u<<26)));assert(iph.gsi.ch[3].posted==63);assert(cfg[(0x1040+0x80*2)/4]==33 && !cfg[(0x1040+0x80*18)/4]);assert(!(cfg[(0x1008+0x80*18)/4]&0x7f));assert((cfg[(0x1024+0x80*18)/4]>>5&63)==6);
 for(unsigned i=0;i<4;i++)assert(cfg[(0x700+4*i)/4]==0x5cd0);
 for(unsigned i=0;i<0x280;i++)assert(local[0x4000+i]==0x6a);
 assert(!sweet_ipa_ready());uint8_t packet[8]={0,1,0,4,0x45,1,2,3};assert(sweet_ipa_transmit(packet,8)==ENETDOWN);
 assert(!sweet_ipa_register_rx(rx_callback,&received));assert(sweet_ipa_register_rx(rx_callback,0)==EBUSY);
 ipa_data_ready(true);assert(sweet_ipa_ready());assert(!sweet_ipa_transmit(packet,8));packet[4]=0xff;
 assert(ipa_data_poll());assert(dma[0x30004]==0x45 && iph.gsi.ch[2].posted==1);
 memcpy(dma+0x40000,dma+0x30000,8);emit(&iph.gsi,3,IPA_DMA_IOVA+iph.gsi.ch[3].ring,8,2);
 assert(ipa_data_poll());assert(received==1 && iph.gsi.ch[2].posted==2 && iph.gsi.ch[3].posted==64);
 ipa_data_ready(false);assert(!sweet_ipa_ready());
 /* Completion cannot publish readiness while a response remains queued. */
 ipa_full=true;qcom_qrtr_init(&m.qr,1,qsend,0);assert(!ipa_qrtr_attach(&m.qr));assert(m.qr.local_count==2);
 m.qr.hello_received=1;m.qr.remote_node=0;m.qr.remote_count=1;m.qr.remote[0].service=0x31;m.qr.remote[0].instance=0x201;m.qr.remote[0].node=0;m.qr.remote[0].port=28;m.qr_started=true;
 assert(!ipa_session_poll());qreceive(0x20);qreceive(0x35);qreceive(0x21);assert(!ipa_session_poll());assert(!sweet_ipa_ready());
 m.qcount=0;assert(!ipa_session_poll());assert(sweet_ipa_ready() && iph.logged);
 static uint32_t outbound[QCOM_SMP2P_ITEM_BYTES/4],inbound[QCOM_SMP2P_ITEM_BYTES/4];QcomSmp2p remote;
 qcom_smp2p_init_out(&m.sp,outbound,0,1);qcom_smp2p_init_out(&remote,inbound,1,0);m.sp.in=inbound;
 ipa_smp_index=qcom_smp2p_add_entry(&m.sp,"ipa");int ri=qcom_smp2p_add_entry(&remote,"ipa");
 ipa.clock_pinned=true;ipa_clock_poll();assert(!qcom_smp2p_out_value(&m.sp,ipa_smp_index));qcom_smp2p_update(&remote,ri,1,1);ipa_clock_poll();assert(qcom_smp2p_out_value(&m.sp,ipa_smp_index)==3 && rings);
 ipa_data_ready(false);ipa_full=false;
 assert(ipa_hardware_quiesce());assert(iph.gsi.claimed && iph.count && !iph.ready);
 for(unsigned i=0;i<4;i++){assert(iph.gsi.ch[i].channel_owned && iph.gsi.ch[i].event_owned && !iph.gsi.ch[i].started);assert(cfg[(0x700+4*i)/4]==0x5cd0);}
 assert(!ipa_gsi_post(&iph.gsi,3,8192,0,0));
 assert(ipa_hardware_stop());assert(!iph.gsi.claimed && !iph.count);
 for(unsigned i=0;i<4;i++)assert(!cfg[(0x700+4*i)/4]);
 for(unsigned n=1;n<=7;n++){hreset();fail_immediate=n;assert(ipa_hardware_setup());assert(!iph.ready);assert(ipa_hardware_stop());}
 hreset();corrupt_read=true;assert(ipa_hardware_setup());assert(ipa_hardware_stop());
 hreset();regs[ipa_gch(20,0)/4]=1u<<20;assert(ipa_hardware_setup()==EBUSY);assert(!cfg_writes && !writes);
 /* Refusal survey keeps measured count bounds and never changes hardware. */
 const unsigned bad_gsi[][2]={{0x25000,0},{0x25040,9},{0x25040,129},{0x25050,3},{0x25050,129},{0x25200,1},{0x2520c,2},{0x1c000+0x80*16,1u<<20}};
 for(unsigned k=0;k<sizeof(bad_gsi)/sizeof(bad_gsi[0]);k++){
  hreset();regs[0x25040/4]=0xe4046312;regs[0x25050/4]=0x2011;regs[bad_gsi[k][0]/4]=bad_gsi[k][1];
  assert(ipa_hardware_setup()==EBUSY);assert(!cfg_writes && !writes && !iph.touched && !iph.gsi.claimed);
 }
 hreset();cfg[0x40/4]=0;assert(ipa_hardware_setup()==EINVAL);assert(!cfg_writes && !writes);
 hreset();assert(!ipa_hardware_setup());cfg[0x100/4]=1u<<18;hold_aggregate=true;assert(!ipa_hardware_stop());assert(iph.gsi.claimed);
 hreset();assert(!ipa_hardware_setup());cfg_drop=cfg_writes+2;assert(!ipa_hardware_stop());assert(iph.count);
 /* A bound-service loss must drop queued TX before the worker drains it. */
 hreset();assert(!ipa_hardware_setup());ipa_data_terminal=false;ipa_full=true;
 memset(&m,0,sizeof(m));qcom_qrtr_init(&m.qr,1,qsend,0);assert(!ipa_qrtr_attach(&m.qr));
 m.qr.hello_received=1;m.qr.remote_node=0;m.qr.remote_count=1;m.qr.remote[0].service=0x31;m.qr.remote[0].instance=0x201;m.qr.remote[0].node=0;m.qr.remote[0].port=28;m.qr_started=true;
 assert(!ipa_session_poll());qreceive(0x20);qreceive(0x35);qreceive(0x21);assert(!ipa_session_poll());m.qcount=0;assert(!ipa_session_poll());assert(sweet_ipa_ready());
 assert(!sweet_ipa_transmit(packet,8));unsigned prior_posted=iph.gsi.ch[2].posted;
 m.qr.remote_count=0;assert(ipa_session_poll()==EIO);assert(!sweet_ipa_ready() && !ipa_txq.count && iph.gsi.ch[2].posted==prior_posted);
 ipa_data_ready(true);assert(!sweet_ipa_ready());assert(ipa_hardware_stop());
 /* Real QRTR dispatch revokes between DEL_SERVER and a following data reply. */
 hreset();assert(!ipa_hardware_setup());ipa_data_terminal=false;ipa_full=true;memset(&m,0,sizeof(m));
 qcom_qrtr_init(&m.qr,1,qsend,0);assert(!ipa_qrtr_attach(&m.qr));assert(!qcom_qrtr_add_service(&m.qr,0,0,0x4010,data_reply,0));
 m.qr.hello_received=1;m.qr.remote_node=0;m.qr.remote_count=1;m.qr.remote[0].service=0x31;m.qr.remote[0].instance=0x201;m.qr.remote[0].node=0;m.qr.remote[0].port=28;m.qr_started=true;m.data_attached=m.data_ipa_bound=true;
 assert(!ipa_session_poll());qreceive(0x20);qreceive(0x35);qreceive(0x21);assert(!ipa_session_poll());m.qcount=0;assert(!ipa_session_poll());ipa_transport_sync();assert(data_ready);
 uint8_t del[52]={0};word(del,1);word(del+4,QCOM_QRTR_DEL_SERVER);word(del+12,QCOM_QRTR_PORT_CTRL);word(del+20,20);word(del+24,1);word(del+28,QCOM_QRTR_PORT_CTRL);word(del+32,QCOM_QRTR_DEL_SERVER);word(del+36,0x31);word(del+40,0x201);word(del+48,28);
 m_greceive(0,0,del,52);assert(!data_ready && iph.qmi.failed && ipa_data_terminal);
 uint8_t reply[33]={0};word(reply,1);word(reply+4,QCOM_QRTR_DATA);word(reply+12,80);word(reply+20,1);word(reply+24,1);word(reply+28,0x4010);
 m_greceive(0,0,reply,sizeof(reply));assert(data_callbacks==1);m_gstate(0,0,0);assert(m.failed && !data_ready);
 assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 /* A malformed completion revokes before any following RX callback. */
 hreset();assert(!ipa_hardware_setup());ipa_data_terminal=false;ipa_data_ready(true);unsigned prior_received=received;
 emit(&iph.gsi,1,IPA_DMA_IOVA+iph.gsi.ch[1].ring+16,8,2);
 assert(!ipa_data_poll());assert(!sweet_ipa_ready() && received==prior_received && ipa_data_terminal);
 ipa_data_ready(true);assert(!sweet_ipa_ready());assert(ipa_hardware_stop());
 /* Stop flushes copied TX and blocks revival while channel backing stays owned. */
 hreset();assert(!ipa_hardware_setup());ipa_data_terminal=false;ipa_data_ready(true);assert(!sweet_ipa_transmit(packet,8));
 assert(!ipa_data_revoke());assert(!ipa_txq.count && !sweet_ipa_ready());assert(ipa_hardware_quiesce());
 for(unsigned i=0;i<4;i++)assert(!iph.gsi.ch[i].started && iph.gsi.ch[i].channel_owned && iph.gsi.ch[i].event_owned);
 ipa_data_ready(true);assert(!sweet_ipa_ready());assert(ipa_hardware_stop());
 puts("IPA hardware, immediate commands, SRAM readback and teardown faults PASS");return 0;
}

static void handshake_stage(bool publish){
 memset(&ipa_txq,0,sizeof(ipa_txq));data_ready=false;
 hreset();assert(!ipa_hardware_setup());ipa_data_terminal=false;ipa_full=true;
 memset(&m,0,sizeof(m));qcom_qrtr_init(&m.qr,1,qsend,0);assert(!ipa_qrtr_attach(&m.qr));
 m.qr.hello_received=1;m.qr.remote_node=0;m.qr.remote_count=1;
 m.qr.remote[0].service=0x31;m.qr.remote[0].instance=0x201;m.qr.remote[0].node=0;m.qr.remote[0].port=28;
 m.qr_started=m.data_attached=m.data_ipa_bound=true;
 assert(!ipa_session_poll());qreceive(0x20);qreceive(0x35);qreceive(0x21);
 assert(!ipa_session_poll());assert(!sweet_ipa_ready());m.qcount=0;
 if(publish){assert(!ipa_session_poll());ipa_transport_sync();assert(sweet_ipa_ready() && data_ready);}
}
static void good_handshake(void){handshake_stage(true);}
int main(void){
 /* Out-path synchronization before preparation must never call an empty IO. */
 ipa_full=true;assert(!iph.gsi.claimed && !iph.gsi.io.read);ipa_transport_sync();
 assert(!sweet_ipa_ready() && !data_ready);ipa_full=false;ipa_data_terminal=false;
 supplied_hardware_main();
 good_handshake();unsigned before=received;uint8_t packet[8]={0,1,0,4,0x45,1,2,3};
 assert(!sweet_ipa_transmit(packet,8));
 emit(&iph.gsi,1,IPA_DMA_IOVA+iph.gsi.ch[1].ring+16,8,2);
 emit(&iph.gsi,3,IPA_DMA_IOVA+iph.gsi.ch[3].ring,8,2);
 assert(ipa_session_poll()==EIO);assert(!sweet_ipa_ready() && !ipa_txq.count && received==before);
 m.failed=true;ipa_transport_sync();assert(!data_ready);ipa_data_ready(true);assert(!sweet_ipa_ready());
 assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 good_handshake();assert(!sweet_ipa_transmit(packet,8));before=received;
 memcpy(dma+iph.gsi.ch[3].buffer,packet,8);
 emit(&iph.gsi,3,IPA_DMA_IOVA+iph.gsi.ch[3].ring,8,2);
 cfg[0x100/4]=(1u<<11)|(1u<<18);m.data_stopping=true;ipa_transport_sync();
 assert(!data_ready && !sweet_ipa_ready() && !ipa_txq.count);
 assert(ipa_data_quiesce());assert(!cfg[0x100/4] && received==before);
 for(unsigned i=0;i<4;i++)assert(!iph.gsi.ch[i].started && iph.gsi.ch[i].channel_owned && iph.gsi.ch[i].event_owned);
 ipa_data_ready(true);assert(!sweet_ipa_ready());assert(ipa_hardware_stop());
 /* Both registers fail closed before publication and after actual I3. */
 const unsigned faults[][2]={{0x25240,0x02301000},{0x2520c,2},{0x25200,1}};
 for(unsigned k=0;k<3;k++)for(unsigned published=0;published<2;published++){
  handshake_stage(published);before=received;
  if(published)assert(!sweet_ipa_transmit(packet,8));
  memcpy(dma+iph.gsi.ch[3].buffer,packet,8);emit(&iph.gsi,3,IPA_DMA_IOVA+iph.gsi.ch[3].ring,8,2);
  unsigned posted=iph.gsi.ch[2].posted,rx_posted=iph.gsi.ch[3].posted;
  regs[faults[k][0]/4]=faults[k][1];assert(ipa_session_poll()==EIO);
  assert(iph.gsi.failed && ipa_data_terminal && !sweet_ipa_ready() && !ipa_txq.count);
  assert(iph.logged==(bool)published && received==before);
  assert(iph.gsi.ch[2].posted==posted && iph.gsi.ch[3].posted==rx_posted);
  ipa_transport_sync();assert(m.failed && !data_ready);
  regs[faults[k][0]/4]=0;ipa_data_ready(true);assert(!sweet_ipa_ready());
  assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 }
 /* The actual QRTR receive precheck revokes before the next data callback. */
 for(unsigned k=0;k<3;k++){
  good_handshake();assert(!qcom_qrtr_add_service(&m.qr,0,0,0x4010,data_reply,0));
  unsigned callbacks=data_callbacks;regs[faults[k][0]/4]=faults[k][1];
  uint8_t reply[33]={0};word(reply,1);word(reply+4,QCOM_QRTR_DATA);word(reply+12,80);word(reply+20,1);word(reply+24,1);word(reply+28,0x4010);
  m_greceive(0,0,reply,sizeof(reply));assert(data_callbacks==callbacks+1 && m.failed && !data_ready);
  assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 }
 /* A fault arriving during event inspection cannot deliver RX or repost it. */
 good_handshake();before=received;unsigned posted=iph.gsi.ch[3].posted;
 memcpy(dma+iph.gsi.ch[3].buffer,packet,8);emit(&iph.gsi,3,IPA_DMA_IOVA+iph.gsi.ch[3].ring,8,2);
 fault_on_producer=true;assert(ipa_session_poll()==EIO);
 assert(!sweet_ipa_ready() && received==before && iph.gsi.ch[3].posted==posted && !iph.gsi.ch[3].completed);
 ipa_transport_sync();assert(m.failed && !data_ready);assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 good_handshake();fault_on_producer=true;assert(ipa_session_poll()==EIO);
 assert(!sweet_ipa_ready());ipa_transport_sync();assert(m.failed && !data_ready);assert(ipa_data_quiesce());assert(ipa_hardware_stop());
 puts("PASS: hardware ERROR_LOG and BUS_ERROR revoke before I3, callbacks, RX and TX; uncertain stop retains DMA");
 puts("PASS: real handshake then GSI failure, and stop with queued TX and pending active RX");return 0;
}
