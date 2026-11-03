#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "QcomSmem.h"
#define M_SMEM 0x00900000ull
#define M_SM_SIZE 0x200000u
#define Z_WAITOK 0
#define Z_ZERO 0
#define IPA_FW_METADATA_MAX 65536u
typedef uintptr_t vm_offset_t;
struct wl_fat {int unused;};
static uint32_t smmu[0x100000/4],smem[M_SM_SIZE/4];
static uint8_t mdt[164];static unsigned cb_reads[72],allocations,freed;
static void put32(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static void *kalloc_data(unsigned size,unsigned flags){(void)flags;allocations++;return calloc(1,size);}
static void kfree_data(void *p,unsigned size){(void)size;freed++;free(p);}
static vm_offset_t ml_io_map(uint64_t pa,unsigned bytes){
 if(pa==0x15000000){assert(bytes==sizeof(smmu));return (uintptr_t)smmu;}
 assert(pa==M_SMEM && bytes==M_SM_SIZE);return (uintptr_t)smem;
}
static uint32_t m_rd(vm_offset_t base,unsigned off){
 assert(base==(uintptr_t)smmu && off<sizeof(smmu) && !(off&3));
 if(off>=0x80000){unsigned cb=(off-0x80000)/4096;assert(cb<72);cb_reads[cb]++;}
 return smmu[off/4];
}
static int fat_open(const struct wl_fat *f,const char *name,uint32_t *cl,uint32_t *size){
 (void)f;if(!strcmp(name,"image/ipa_fws.mdt")){*cl=1;*size=sizeof(mdt);}else{assert(!strcmp(name,"image/ipa_fws.b02"));*cl=2;*size=32;}return 0;
}
static int fat_read_chain(const struct wl_fat *f,uint32_t cl,uint32_t off,void *dst,uint32_t size){
 (void)f;if(cl==1){assert(off+size<=sizeof(mdt));memcpy(dst,mdt+off,size);}else{assert(cl==2 && off+size<=32);memset(dst,0x17,size);}return 0;
}
static void sweet_log_sync(void){}
static int quiet_printf(const char *format,...){(void)format;return 0;}
#define printf quiet_printf
#include "IpaProbe.inc"
#undef printf
int main(void){
 mdt[0]=127;memcpy(mdt+1,"ELF",3);mdt[4]=mdt[5]=mdt[6]=1;mdt[16]=2;put32(mdt+20,1);put32(mdt+28,52);mdt[40]=52;mdt[42]=32;mdt[44]=3;
 put32(mdt+52+16,148);put32(mdt+84+16,16);put32(mdt+84+24,0x02000000);put32(mdt+116,1);put32(mdt+116+12,0x10000);put32(mdt+116+16,32);put32(mdt+116+20,64);put32(mdt+116+24,0x08000000);
 smmu[0x20/4]=0x4c017e72;smmu[0x24/4]=0x60000048;smmu[0x28/4]=0x5111;
 for(unsigned i=0;i<72;i++)smmu[(0x80000+4096*i)/4]=0xe0;
 smmu[0x800/4]=0x800004a0;smmu[0xc00/4]=3;
 static uint32_t before[sizeof(smmu)/4];memcpy(before,smmu,sizeof(smmu));
 struct wl_fat f={0};assert(!ipa_preflight(&f));assert(allocations==freed);
 assert(!memcmp(before,smmu,sizeof(smmu)));
 for(unsigned i=0;i<72;i++)assert(cb_reads[i]>=6);
 for(unsigned i=0;i<M_SM_SIZE/4;i++)assert(smem[i]==0); /* No partition mutation even on missing TOC. */
 memset(cb_reads,0,sizeof(cb_reads));smmu[0x24/4]=0xe0000048;assert(ipa_preflight(&f)==ENOTSUP);
 for(unsigned i=0;i<72;i++)assert(!cb_reads[i]);
 puts("IPA read-only context/SMEM survey bounds and no-write checks PASS");
}
