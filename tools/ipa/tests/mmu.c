#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "IpaMmu.h"
static uint32_t reg[0x100000/4],original[0x100000/4];
static unsigned writes,fail_at;static bool stalled,force_tbi;static uint32_t reset_ctl;
static uint32_t rd(void *c,unsigned a){(void)c;assert(a<0x100000);if(stalled && (a&4095)==0x7f4)return 1;return reg[a/4];}
static void wr(void *c,unsigned a,uint32_t v){(void)c;assert(a<0x100000);writes++;if(writes!=fail_at){
 if(a>=0x80000){unsigned bank=(a-0x80000)/4096,off=a&4095;
  if(off==0x618){assert((reg[(0x1000+4*bank)/4]&255)!=0);assert(!reg[ipa_cb(bank)/4]);}
  /* Ast171 observed TBI0/1 set. Model that device behavior, not reserved bits. */
  if(off==0x10 && force_tbi)v|=0x60;
  if(off==0){
   assert(v==0 || v==(1|2|4|0x20|0x1000|(3u<<22)) || v==(1|2|4|0x20|(3u<<22)));
   v&=~0x1000u; /* ast173: ASIDPNE reads zero. Old request fails install. */
  }
 }
 if(a>=0xc00 && a<0xdc8 && ((v>>16)&3)==1)assert(!(reg[(a-0xc00+0x800)/4]>>31));
 reg[a/4]=v;
}}
static void w64(void *c,unsigned a,uint64_t v){
 unsigned bank=(a-0x80000)/4096;
 assert(reg[(0x1800+4*bank)/4]==1 && !(v&4095) && !(v>>56));
 assert((v&0xffffffffffffull)<(1ull<<36)); /* Sampled UNKNOWN TTBR fails this. */
 wr(c,a,(uint32_t)v);wr(c,a+4,(uint32_t)(v>>32));
}
static void delay1(void *c){(void)c;}
static IpaMmu reset(void){
 memset(reg,0,sizeof(reg));reg[0x20/4]=0x4c017e72;reg[0x24/4]=0x60000048;reg[0x28/4]=0x5111;
 reg[0x800/4]=0x80001c00;reg[0xc00/4]=0;reg[0x80000/4]=1;
 reg[0x804/4]=0x1234;reg[0xc04/4]=1<<16;
 for(unsigned i=1;i<72;i++){
  reg[ipa_cb(i)/4]=reset_ctl;
  if(reset_ctl)for(unsigned j=1;j<9;j++)reg[(ipa_cb(i)+ipa_mmu_regs[j])/4]=0x9137a2c0u+i*0x1234+j;
  if(reset_ctl){reg[(ipa_cb(i)+0x20)/4]=0x73bf763b;reg[(ipa_cb(i)+0x24)/4]=0x9191f7ef;}
 }
 memcpy(original,reg,sizeof(reg));writes=0;fail_at=0;stalled=false;
 return (IpaMmu){.io={0,rd,wr,w64,delay1}};
}
static void parked(IpaMmu *m){
 for(unsigned n=0;n<2;n++){IpaMmuContext *p=&m->context[n];unsigned b=ipa_cb(p->bank);
  assert(reg[(0x800+4*p->slot)/4]==p->smr && reg[(0xc00+4*p->slot)/4]==p->s2cr);
  assert(!reg[b/4] && !reg[(b+0x20)/4] && !reg[(b+0x24)/4] && !reg[(b+0x28)/4] && !reg[(b+0x2c)/4]);
  assert(reg[(0x1800+4*p->bank)/4]==1 && reg[(0x1000+4*p->bank)/4]==(0x1f300|p->vmid));
  assert(!p->touched && !p->published && !p->configured);
 }
}
static void run(void){
 const uint64_t pa[]={0x8c1000000ull,0x8c1010000ull};
 IpaMmu m=reset();assert(ipa_mmu_install(&m,pa));assert(m.context[0].bank==1 && m.context[1].bank==2);
 assert(reg[(ipa_cb(m.context[0].bank)+0x10)/4]==IPA_MMU_TCR2);
 unsigned installed_writes=writes;
 assert(ipa_mmu_release(&m,true));parked(&m);assert(!memcmp(original,reg,0x800));assert(reg[0x800/4]==original[0x800/4]);
 /* Parked contexts/VMIDs stay out of the next lease. */
 IpaMmu next={.io={0,rd,wr,w64,delay1}};assert(ipa_mmu_install(&next,pa));assert(next.context[0].bank==3 && next.context[0].vmid==3);assert(ipa_mmu_release(&next,true));
 m=reset();const uint64_t oversized[]={1ull<<36,0x8c1010000ull};assert(!ipa_mmu_install(&m,oversized) && !writes);
 m=reset();reg[0x808/4]=0x800004a0;assert(!ipa_mmu_install(&m,pa) && !writes);
 m=reset();reg[0x24/4]^=1;assert(!ipa_mmu_install(&m,pa) && !writes);
 m=reset();assert(ipa_mmu_install(&m,pa));unsigned before=writes;assert(!ipa_mmu_release(&m,false));assert(writes==before && m.retained);
 m=reset();stalled=true;assert(!ipa_mmu_install(&m,pa));assert(m.retained);before=writes;assert(!ipa_mmu_release(&m,true) && writes==before);
 for(unsigned n=1;n<=installed_writes;n++){
  m=reset();fail_at=n;bool ok=ipa_mmu_install(&m,pa);fail_at=0;
  if(ok){assert(!m.retained);assert(ipa_mmu_release(&m,true));parked(&m);}
  else { /* Runtime refuses all further MMIO after any touched install failure. */
   if(m.context[0].touched || m.context[1].touched)m.retained=true;
   before=writes;bool clean=ipa_mmu_release(&m,true);assert(clean || m.retained);assert(writes==before);
  }
  assert(reg[0x800/4]==original[0x800/4] && reg[0x80000/4]==1);
 }
 m=reset();assert(ipa_mmu_install(&m,pa));writes=0;assert(ipa_mmu_release(&m,true));unsigned release_writes=writes;
 for(unsigned n=1;n<=release_writes;n++){
  m=reset();assert(ipa_mmu_install(&m,pa));writes=0;fail_at=n;
  bool clean=ipa_mmu_release(&m,true);if(clean)parked(&m);else assert(m.retained);
 }
 /* Direct partial-release calls also fail closed without writes. */
 m=reset();m.context[0].touched=true;assert(!ipa_mmu_release(&m,true) && !writes && m.retained);
 puts("IPA SMMU private leases, known-format parking and dropped-write retention PASS");
}
int main(void){
 for(unsigned t=0;t<2;t++){force_tbi=t;reset_ctl=0;run();reset_ctl=0x3ec9850c;run();reset_ctl=0x3ed0030c;run();}
 const uint64_t pa[]={0x8c1000000ull,0x8c1010000ull};
 for(unsigned bit=0;bit<32;bit++)if((1u<<bit)&0xe1){IpaMmu m=reset();for(unsigned i=1;i<72;i++)reg[ipa_cb(i)/4]=reset_ctl|(1u<<bit);assert(!ipa_mmu_install(&m,pa) && !writes);}
 for(unsigned k=0;k<3;k++){IpaMmu m=reset();for(unsigned i=1;i<72;i++)reg[(k==0?ipa_cb(i)+0x58:k==1?0x1000+4*i:0x1800+4*i)/4]=1;assert(!ipa_mmu_install(&m,pa) && !writes);}
 IpaMmu m=reset();reg[0]=1u<<3;assert(!ipa_mmu_install(&m,pa) && !writes);
 m=reset();reg[0x1000/4]=(3u<<16)|(1u<<8);assert(ipa_mmu_install(&m,pa));assert(m.context[0].bank==2 && m.context[1].bank==3);assert(ipa_mmu_release(&m,true));
 m=reset();reg[0x1000/4]=(3u<<16)|(72u<<8);assert(!ipa_mmu_install(&m,pa) && !writes);
 m=reset();reg[0x1000/4]=(3u<<16)|(1u<<8);reg[0x1004/4]=1u<<16;assert(!ipa_mmu_install(&m,pa) && !writes);
 for(unsigned mode=0;mode<2;mode++){m=reset();reg[0]=mode?1u<<31:1;assert(!ipa_mmu_install(&m,pa) && !writes);}
 m=reset();reg[0x1000/4]=0x1f301;reg[(0x1000+4*10)/4]=0x1f302;assert(ipa_mmu_install(&m,pa));assert(m.context[0].vmid==3 && m.context[1].vmid==4);assert(ipa_mmu_release(&m,true));
 m=reset();assert(ipa_mmu_install(&m,pa));reg[0]=1u<<31;writes=0;assert(!ipa_mmu_release(&m,true) && !writes && m.retained);
 /* TBI is programmed, never masked: a different physical width is refused. */
 m=reset();reg[0x28/4]=0x5211;assert(!ipa_mmu_install(&m,pa) && !writes);
 puts("Measured UNKNOWN TTBR is never replayed; TBI, mode and ownership checks PASS");return 0;
}
