#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "IpaMmu.h"
#include "IpaSmmu.h"
static uint32_t regs[0x100000/4];
static uint64_t tables[2][IPA_PT_PAGES*512];
static struct {bool valid,global;unsigned vmid,asid;uint64_t va,pa;} cache[8];
static uint32_t rd(void *c,unsigned a){(void)c;assert(a<sizeof(regs));return regs[a/4];}
static void wr(void *c,unsigned a,uint32_t v){
 (void)c;assert(a<sizeof(regs));regs[a/4]=v;
 if(a>=0x80000 && (a&4095)==0x618){
  unsigned bank=(a-0x80000)/4096,vmid=regs[(0x1000+4*bank)/4]&255;
  assert(vmid && !(regs[ipa_cb(bank)/4]&1));
  for(unsigned i=0;i<8;i++)if(cache[i].valid && cache[i].vmid==vmid)cache[i].valid=false;
 }
}
static void w64(void *c,unsigned a,uint64_t v){wr(c,a,v);wr(c,a+4,v>>32);}
static void wait1(void *c){(void)c;}
static uint64_t access(IpaMmuContext *c,IpaPageTable *pt,uint64_t va){
 unsigned vmid=regs[(0x1000+4*c->bank)/4]&255,asid=regs[(ipa_cb(c->bank)+0x24)/4]>>16;
 assert(regs[ipa_cb(c->bank)/4]&1);
 for(unsigned i=0;i<8;i++)if(cache[i].valid && cache[i].vmid==vmid && cache[i].va==va && (cache[i].global || cache[i].asid==asid))return cache[i].pa;
 unsigned page=0,shift[]={30,21,12};uint64_t entry=0;
 for(unsigned i=0;i<3;i++){
  entry=pt->words[page*512+((va>>shift[i])&511)];if(!(entry&1))return 0;
  if(i<2){assert((entry&3)==3);page=(unsigned)(((entry&0xfffffffff000ull)-pt->physical)/4096);assert(page<pt->used);}
 }
 uint64_t pa=entry&0xfffffffff000ull;unsigned slot;
 for(slot=0;slot<8 && cache[slot].valid;slot++);assert(slot<8);
 cache[slot]=(typeof(cache[0])){true,!(entry&(1ull<<11)),vmid,asid,va,pa};return pa;
}
int main(void){
 const uint64_t pa[]={0x8c1000000ull,0x8c1010000ull},va=0x20000000,owned=0x8c2000000ull;
 regs[0x20/4]=0x4c017e72;regs[0x24/4]=0x60000048;regs[0x28/4]=0x5111;
 IpaPageTable pt[2];for(unsigned i=0;i<2;i++)assert(!ipa_pt_init(&pt[i],tables[i],pa[i]));
 assert(!ipa_pt_map(&pt[0],va,owned,4096));
 cache[0]=(typeof(cache[0])){true,true,0,0,va,0xdead0000};
 cache[1]=(typeof(cache[0])){true,true,1,0,va,0xbeef0000};
 IpaMmu m={.io={0,rd,wr,w64,wait1}};assert(ipa_mmu_install(&m,pa));
 assert(m.context[0].vmid==1 && m.context[1].vmid==2 && cache[0].valid && !cache[1].valid);
 assert(access(&m.context[0],&pt[0],va)==owned);
 assert(!cache[1].global); /* Every mapped leaf must carry nG. */
 assert(!access(&m.context[1],&pt[1],va));
 /* Negative control: the old shared-VMID/global-entry design crosses domains. */
 cache[1].global=true;regs[(0x1000+4*m.context[1].bank)/4]=(0x1f300|m.context[0].vmid);
 assert(access(&m.context[1],&pt[1],va)==owned);
 regs[(0x1000+4*m.context[1].bank)/4]=(0x1f300|m.context[1].vmid);cache[1].global=false;
 assert(ipa_mmu_release(&m,true));assert(cache[0].valid && !cache[1].valid);
 puts("PASS: real descriptors/VMID leases isolate AP and empty uC in a shared TLB; stale leases flush, inherited VMID0 survives");
}
