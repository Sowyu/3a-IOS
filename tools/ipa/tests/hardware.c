#define IPA_HOST_TEST 1
/* Compile the real hardware/table driver against a bounded GSI+SRAM model. */
#define main ipa_gsi_test_main
#include "gsi.c"
#undef main
#include <errno.h>
#define PAGE_SIZE 16384u
#define IPA_DMA_IOVA 0x20000000u
typedef uintptr_t vm_offset_t;
static struct {vm_offset_t dma,block;bool clock_pinned;uint32_t smem_bytes;} ipa;
static uint32_t cfg[0x10000/4];static uint8_t local[0x10000];
static unsigned cfg_writes,cfg_drop,immediates,fail_immediate,cmd_seen;
static bool corrupt_read,hold_aggregate;
static uint32_t m_rd(vm_offset_t p,unsigned o){assert(p==ipa.block);if(o>=0x140000){assert(o<0x150000);return cfg[(o-0x140000)/4];}assert(o>=0x4000);return rd(0,o-0x4000);}
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
static struct {QcomSmp2p sp;QcomQrtr qr;bool qr_started,failed,fatal;unsigned qcount;} m;
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
static unsigned received;
static void rx_callback(void *ctx,const uint8_t *p,size_t n){assert(ctx==&received && !queue_locked);assert(n==8 && p[4]==0x45);received++;assert(!sweet_ipa_transmit(p,n));}
static void hreset(void){reset();memset(cfg,0,sizeof(cfg));memset(local,0x6a,sizeof(local));ipa.smem_bytes=0xb000;ipa.dma=(uintptr_t)dma-PAGE_SIZE;ipa.block=1;cfg[0x40/4]=(0x800u<<16)|0x800;cfg[0]=24|(12u<<16);cfg_writes=cfg_drop=immediates=fail_immediate=cmd_seen=0;corrupt_read=hold_aggregate=false;}
int main(void){
 hreset();assert(!ipa_hardware_setup());assert(immediates==7 && iph.ready);assert(cfg[0x38/4]==(11|(11u<<8)|(3u<<26)));assert(iph.gsi.ch[3].posted==63);assert(cfg[(0x1040+0x80*2)/4]==33 && !cfg[(0x1040+0x80*18)/4]);assert(!(cfg[(0x1008+0x80*18)/4]&0x7f));assert((cfg[(0x1024+0x80*18)/4]>>5&63)==6);
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
 ipa_data_ready(false);ipa_full=false;assert(ipa_hardware_stop());assert(!iph.gsi.claimed && !iph.count);
 for(unsigned n=1;n<=7;n++){hreset();fail_immediate=n;assert(ipa_hardware_setup());assert(!iph.ready);assert(ipa_hardware_stop());}
 hreset();corrupt_read=true;assert(ipa_hardware_setup());assert(ipa_hardware_stop());
 hreset();regs[ipa_gch(20,0)/4]=1u<<20;assert(ipa_hardware_setup()==EBUSY);assert(!cfg_writes && !writes);
 hreset();cfg[0x40/4]=0;assert(ipa_hardware_setup()==EINVAL);assert(!cfg_writes && !writes);
 hreset();assert(!ipa_hardware_setup());cfg[0x100/4]=1u<<18;hold_aggregate=true;assert(!ipa_hardware_stop());assert(iph.gsi.claimed);
 hreset();assert(!ipa_hardware_setup());cfg_drop=cfg_writes+2;assert(!ipa_hardware_stop());assert(iph.count);
 puts("IPA hardware, immediate commands, SRAM readback and teardown faults PASS");return 0;
}
