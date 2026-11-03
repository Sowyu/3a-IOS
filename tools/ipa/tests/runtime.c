#define _POSIX_C_SOURCE 200112L
#define IPA_HOST_TEST 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "IpaFirmware.h"
#include "QcomScm.h"
#include "QcomRpmh.h"
typedef uintptr_t vm_offset_t,addr64_t;typedef size_t vm_size_t;typedef unsigned long ppnum_t;typedef void *pmap_t;
#define PAGE_SIZE 16384u
#define PAGE_MASK 16383u
#define KMA_DATA 1
#define KMA_ZERO 2
#define VM_KERN_MEMORY_IOKIT 0
#define KERN_SUCCESS 0
#define VM_WIMG_IO 0
#define VM_WIMG_WCOMB 1
#define ptoa_64(x) ((uint64_t)(x)<<14)
static void *kernel_map,*kernel_pmap;
struct wl_fat {int unused;};
#define M_SMEM 0x81d00000ull
#define M_SM_SIZE 0x200000u
static struct {bool bandwidth_pinned,bandwidth_irq,vote_uncertain;vm_offset_t rsc,db,gic;} m;
static unsigned freed,unmapped,alloc_calls,fail_alloc,pas_calls[8],fail_pas,shutdown_fails;
static bool held,timeout,drop_irq_enable,drop_irq_clear;
static uint32_t gic[0x1000/4];
_Alignas(PAGE_SIZE) static uint32_t smmu[0x100000/4],rsc[0x4000/4],block[0x200000/4];
_Alignas(PAGE_SIZE) static uint8_t firmware[0x10000],mdt[256];static unsigned mdt_bytes;
static struct {void *ptr;size_t size;uint64_t pa;} allocation[8];static unsigned allocations;
static unsigned clocks;
static size_t mapped_size[3];
static int kmem_alloc_contig(void *map,vm_offset_t *va,size_t size,unsigned mask,unsigned a,unsigned b,unsigned flags,unsigned tag){
 (void)map;(void)mask;(void)a;(void)b;(void)flags;(void)tag;
 if(++alloc_calls==fail_alloc)return ENOMEM;
 void *p=NULL;assert(!posix_memalign(&p,PAGE_SIZE,size));memset(p,0,size);
 allocation[allocations]=(typeof(allocation[0])){p,size,0x8c1000000ull+allocations*0x200000};allocations++;*va=(uintptr_t)p;return 0;
}
ppnum_t pmap_find_phys(pmap_t map,addr64_t va){(void)map;for(unsigned i=0;i<allocations;i++)if(va>=(uintptr_t)allocation[i].ptr && va-(uintptr_t)allocation[i].ptr<allocation[i].size)return (allocation[i].pa+va-(uintptr_t)allocation[i].ptr)>>14;return 0;}
static void kmem_free(void *map,vm_offset_t va,size_t size){(void)map;for(unsigned i=0;i<allocations;i++)if((uintptr_t)allocation[i].ptr==va){assert(allocation[i].size==size);free(allocation[i].ptr);allocation[i].ptr=NULL;freed++;return;}assert(0);}
static vm_offset_t ml_io_map_unmappable(uint64_t pa,size_t size,unsigned flags){
 (void)flags;if(++alloc_calls==fail_alloc)return 0;
 assert(!(pa&PAGE_MASK));unsigned i;vm_offset_t va;
 if(pa==0x15000000){i=0;va=(uintptr_t)smmu;}
 else if(pa==IPA_FW_BASE){i=1;va=(uintptr_t)firmware;assert(size<=sizeof(firmware));}
 else {assert(pa==0x3e00000);i=2;va=(uintptr_t)block;}
 /* Model native io_map's rounded allocation and ml_io_unmap's exact span. */
 mapped_size[i]=(size+PAGE_MASK)&~PAGE_MASK;return va;
}
static void ml_io_unmap(vm_offset_t va,size_t size){
 assert(!((va|size)&PAGE_MASK));
 unsigned i=va==(uintptr_t)smmu?0:va==(uintptr_t)firmware?1:2;
 assert(i!=2 || va==(uintptr_t)block);assert(mapped_size[i]==size);
 mapped_size[i]=0;unmapped++;
}
static void sweet_dma_sync_va(const void *va,size_t size){assert(va && size);}
static uint32_t m_rd(vm_offset_t p,unsigned o){if(p==m.db)return 0;assert(p);if(p==m.rsc && o==0xd04)return rsc[o/4]&rsc[0xd00/4];return *(uint32_t *)(p+o);}
static void m_wr(vm_offset_t p,unsigned o,uint32_t v){
 if(p==m.rsc && ((o==0xd00 && drop_irq_enable) || (o==0xd08 && drop_irq_clear)))return;
 *(uint32_t *)(p+o)=v;
 if(p==m.rsc){
  if(o==0xd08)rsc[0xd04/4]&=~v;
  for(unsigned i=0;i<3;i++)if(o==0xd00+i*0x2a0+0x24 && (v&(1<<24)) && !timeout){rsc[0xd04/4]|=1u<<i;rsc[(0xd00+i*0x2a0+0x40)/4]=0x10100;clocks++;}
 }
}
int sweet_apps_smmu_claim(void){if(held)return 0;held=true;return 1;}
void sweet_apps_smmu_release(void){assert(held);held=false;}
static void sweet_apps_rsc_lock(void){}static void sweet_apps_rsc_unlock(void){}
static void delay(unsigned n){(void)n;}static void m_sleep(unsigned n){(void)n;}static void sweet_log_sync(void){}
static int m_alloc(unsigned id,unsigned size,uint32_t *off){assert(id==497 && size==0xb000);*off=0x10000;return 0;}
static int m_item(unsigned id,unsigned size,uint32_t *off,uint32_t *actual){assert(id==497 && size==0xb000);*off=0x10000;*actual=0xb000;return 0;}
int qcom_cmd_db_find(const uint8_t *db,size_t size,const char *name,QcomRpmhResource *r){
 (void)db;(void)size;assert(!strcmp(name,"CN0") || !strcmp(name,"IP0"));memset(r,0,sizeof(*r));r->address=!strcmp(name,"CN0")?0x50020:0x50038;r->slave_id=5;r->aux_version=0x1000;r->aux_bytes=8;r->aux_prefix[0]=1;if(!strcmp(name,"IP0")){r->aux_prefix[0]=40;r->aux_prefix[4]=8;r->aux_prefix[6]=7;}return 0;
}
static int m_scm(uint32_t fn,uint32_t info,uint64_t pas,uint64_t a,uint64_t b,uint64_t c,uint64_t *result){
 (void)info;(void)a;(void)b;(void)c;assert(pas==15);unsigned cmd=fn&255;assert(cmd<8);pas_calls[cmd]++;*result=cmd==7?1:0;
 if(cmd==fail_pas || (cmd==6 && shutdown_fails))return -1;return 0;
}
static int fat_open(const struct wl_fat *f,const char *name,uint32_t *cl,uint32_t *size){(void)f;if(!strcmp(name,"image/ipa_fws.mdt")){*cl=1;*size=mdt_bytes;return 0;}assert(!strcmp(name,"image/ipa_fws.b02"));*cl=2;*size=32;return 0;}
static int fat_read_chain(const struct wl_fat *f,uint32_t cl,uint32_t off,void *dst,uint32_t size){(void)f;if(cl==1){assert(off+size<=mdt_bytes);memcpy(dst,mdt+off,size);}else{assert(cl==2 && off+size<=32);memset(dst,0x17,size);}return 0;}
static int quiet_printf(const char *format,...){(void)format;return 0;}
#define printf quiet_printf
#include "IpaRuntime.inc"
#undef printf
static void put32(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static void reset(void){
 /* Test process owns all retained allocations after the simulated reboot. */
 for(unsigned i=0;i<allocations;i++)if(allocation[i].ptr)free(allocation[i].ptr);
 memset(mapped_size,0,sizeof(mapped_size));
 allocations=freed=unmapped=alloc_calls=fail_alloc=fail_pas=shutdown_fails=clocks=0;held=timeout=drop_irq_enable=drop_irq_clear=false;memset(pas_calls,0,sizeof(pas_calls));memset(&ipa,0,sizeof(ipa));ipa.enabled=true;
 memset(smmu,0,sizeof(smmu));smmu[0x20/4]=0x4c017e72;smmu[0x24/4]=0x60000048;smmu[0x28/4]=0x5111;
 for(unsigned i=0;i<72;i++){
  smmu[ipa_cb(i)/4]=0x3ec9850c;
  for(unsigned j=1;j<9;j++)smmu[(ipa_cb(i)+ipa_mmu_regs[j])/4]=0x9191f7efu+i+j;
 }
 memset(rsc,0,sizeof(rsc));for(unsigned i=0;i<3;i++)rsc[(0xd00+i*0x2a0+0x28)/4]=1;
 m.rsc=(uintptr_t)rsc;m.db=1;m.bandwidth_pinned=m.bandwidth_irq=true;m.vote_uncertain=false;
 memset(gic,0,sizeof(gic));m.gic=(uintptr_t)gic;gic[0x424/4]=0xa000;
 memset(firmware,0x6c,sizeof(firmware));memset(mdt,0,sizeof(mdt));mdt[0]=127;memcpy(mdt+1,"ELF",3);mdt[4]=mdt[5]=mdt[6]=1;mdt[16]=2;put32(mdt+20,1);put32(mdt+28,52);mdt[40]=52;mdt[42]=32;mdt[44]=3;
 put32(mdt+52+16,148);put32(mdt+84+16,16);put32(mdt+84+24,0x02000000);put32(mdt+116,1);put32(mdt+116+12,0x10000);put32(mdt+116+16,32);put32(mdt+116+20,64);put32(mdt+116+24,0x08000000);mdt_bytes=164;
}
int main(void){
 struct wl_fat f={0};reset();assert(!ipa_prepare(&f));assert(pas_calls[1]==1 && pas_calls[2]==1 && pas_calls[5]==1);assert(clocks==2);assert(mapped_size[1]==0xc000 && IPA_FW_SIZE==0xa000);unsigned steps=alloc_calls;assert(ipa_cleanup(true));assert(pas_calls[6]==1 && freed==allocations);assert(!mapped_size[0] && !mapped_size[1] && !mapped_size[2]);
 for(unsigned i=0;i<32;i++)assert(firmware[i]==0x17);for(unsigned i=32;i<64;i++)assert(firmware[i]==0);for(unsigned i=64;i<sizeof(firmware);i++)assert(firmware[i]==0x6c);
 for(unsigned n=1;n<=steps;n++){reset();fail_alloc=n;assert(ipa_prepare(&f));assert(ipa_cleanup(true));assert(freed==allocations);}
 for(unsigned cmd=1;cmd<=5;cmd++){if(cmd!=1 && cmd!=2 && cmd!=5)continue;reset();fail_pas=cmd;assert(ipa_prepare(&f));assert(ipa_cleanup(true));assert(pas_calls[6]==1 && freed==allocations);}
 reset();assert(!ipa_prepare(&f));shutdown_fails=1;assert(!ipa_cleanup(true));assert(pas_calls[6]==4 && freed==0 && unmapped==0);
 reset();assert(!ipa_prepare(&f));assert(!ipa_cleanup(false));assert(pas_calls[6]==0 && freed==0 && unmapped==0);
 reset();timeout=true;assert(ipa_prepare(&f));assert(m.vote_uncertain && !pas_calls[1]);assert(!ipa_cleanup(true));assert(!freed && !unmapped);
 reset();gic[0x104/4]=1u<<5;assert(ipa_prepare(&f)==EBUSY);assert(!clocks && !pas_calls[1]);assert(ipa_cleanup(true));
 reset();gic[0x304/4]=1u<<5;assert(ipa_prepare(&f)==EBUSY);assert(!clocks && !pas_calls[1]);
 reset();gic[0x424/4]=0;assert(ipa_prepare(&f)==EBUSY);assert(!clocks && !pas_calls[1]);
 reset();drop_irq_enable=true;assert(ipa_prepare(&f)==EIO);assert(!clocks && !pas_calls[1]);
 reset();rsc[0xd04/4]=1;drop_irq_clear=true;assert(ipa_prepare(&f)==EIO);assert(!clocks && !pas_calls[1]);
 reset();rsc[0xd04/4]=1;assert(!ipa_prepare(&f));assert(clocks==2 && !rsc[0xd00/4]);assert(ipa_cleanup(true));
 reset();mdt[4]=2;assert(ipa_prepare(&f));assert(!pas_calls[1]);assert(ipa_cleanup(true));assert(freed==allocations);
 reset();assert(!ipa_prepare(&f));*(uint8_t *)ipa.dma=0;assert(!ipa_cleanup(true));assert(!freed && !unmapped);
 reset();ipa.map_probe=true;assert(!ipa_prepare(&f));assert(!clocks);
 for(unsigned i=0;i<8;i++)assert(!pas_calls[i]);assert(!ipa.firmware && !ipa.metadata && !ipa.block);
 assert(ipa_cleanup(true) && freed==allocations);for(unsigned i=0;i<8;i++)assert(!pas_calls[i]);
 reset();ipa.map_probe=true;held=true;assert(ipa_prepare(&f)==EBUSY);held=false;assert(ipa_cleanup(true));
 reset();puts("IPA SMMU-only probe skips firmware, PAS, IPA registers and IPA power votes PASS");
 puts("IPA runtime PAS15, reservation bounds, allocation failures and retained teardown PASS");
}
