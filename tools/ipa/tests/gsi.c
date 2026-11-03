#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "IpaGsi.h"
static uint32_t regs[0x40000/4];static uint8_t dma[0x100000] __attribute__((aligned(4096)));
static unsigned writes,commands,fail_command,drop_at,error_inits;static bool init_fault;static bool sync_seen;
static unsigned survey_fault_register,survey_fault_value;
static unsigned fault_sync_offset=~0u,fault_register,fault_value;
static uint32_t rd(void *c,unsigned o){(void)c;assert(o<sizeof(regs));if(survey_fault_register && writes==5 && o==ipa_gev((regs[0x25050/4]&255)-1,0)){regs[survey_fault_register/4]=survey_fault_value;regs[0x25240/4]=0x02301000;survey_fault_register=0;}return regs[o/4];}
static void wr(void *c,unsigned o,uint32_t v){
 (void)c;assert(o<sizeof(regs));assert(o<0x26000);writes++;if(writes==drop_at)return;
 if(o==0x25240){
  assert(v==0 && !regs[0x25200/4] && !regs[0x2520c/4]);
  for(unsigned i=0;i<(regs[0x25040/4]&255);i++)assert(!((regs[ipa_gch(i,0)/4]>>20)&15));
  for(unsigned i=0;i<(regs[0x25050/4]&255);i++)assert(!((regs[ipa_gev(i,0)/4]>>20)&15));
  error_inits++;if(init_fault)regs[0x25200/4]=1;
 }
 assert(o!=0x25244 && o!=0x25208 && o!=0x25214);
 if(o==0x250b0){regs[0x250a8/4]&=~v;return;}
 if(o==0x25098){regs[0x25090/4]&=~v;return;}if(o==0x250a4){regs[0x2509c/4]&=~v;return;}
 if(o==0x25008 || o==0x25010){
  commands++;if(commands==fail_command)return;bool ev=o==0x25010;unsigned id=v&255,op=v>>24,at=ev?ipa_gev(id,0):ipa_gch(id,0),state=(regs[at/4]>>20)&15;
  if(op==0){assert(!state);state=1;}else if(op==1){assert(!ev && state==1);state=2;}
  else if(op==2){assert(!ev && state==2);state=3;}else if(op==9){assert(state==1 || state==3);state=1;}
  else if(op==10){assert(state==1);state=0;}else assert(0);
  regs[at/4]=(regs[at/4]&~(15u<<20))|(state<<20);regs[(ev?0x2509c:0x25090)/4]|=1u<<id;return;
 }
 if((o>=0x14000 && o<0x18000 && !(o&0x7f)) || (o>=0x1c000 && o<0x20000 && !(o&0x7f)))v=(v&~(15u<<20))|(regs[o/4]&(15u<<20));
 regs[o/4]=v;
 if(o>=0x1c008 && o<0x20000 && (o&0x7f)==8)regs[(o+8)/4]=v;
 if(o>=0x24000 && o<=0x24000+9*8 && !(o&7))assert(sync_seen);
}
static void sync1(void *c,unsigned o,unsigned n){(void)c;assert(n && o<=sizeof(dma) && n<=sizeof(dma)-o);sync_seen=true;if(o==fault_sync_offset){regs[fault_register/4]=fault_value;fault_sync_offset=~0u;}}
static void wait1(void *c,unsigned us){(void)c;assert(us<=1000);}
static IpaGsiIo io={0,rd,wr,sync1,wait1};
static void reset(void){memset(regs,0,sizeof(regs));memset(dma,0,sizeof(dma));writes=commands=fail_command=drop_at=error_inits=0;init_fault=false;survey_fault_register=0;sync_seen=false;fault_sync_offset=~0u;regs[0x25000/4]=1;regs[0x25040/4]=32;regs[0x25050/4]=32;}
static void emit(IpaGsi *s,unsigned i,uint64_t address,unsigned len,unsigned code){
 IpaGsiChannel *ch=&s->ch[i];IpaEvent ev={address,len,0,code,0,0x22,ch->channel};memcpy(dma+ch->events+(ch->consumed%ch->slots)*16,&ev,16);regs[ipa_gev(ch->event,4)/4]=s->base+ch->events+((ch->consumed+1)%ch->slots)*16;
}
int main(void){
 IpaGsi s;reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(commands==8);
 assert(regs[ipa_gch(9,1)/4]==(3u<<24|64*16));assert(regs[(0x14054+6*0x80)/4]==(20u*16<<16));assert(regs[(0x1404c+6*0x80)/4]==0);
 for(unsigned i=0;i<4;i++)assert(ipa_gsi_start(&s,i));
 uint8_t payload[24]={0};assert(ipa_gsi_post(&s,0,7,payload,sizeof(payload)));
 unsigned buf,n;emit(&s,0,0x20000000+s.ch[0].ring,24,2);assert(ipa_gsi_event(&s,0,&buf,&n)==1);assert(n==24 && s.ch[0].completed==1);
 for(unsigned i=0;i<63;i++)assert(ipa_gsi_post(&s,3,8192,0,0));assert(!ipa_gsi_post(&s,3,8192,0,0));
 for(unsigned j=0;j<200;j++){
  IpaGsiChannel *ch=&s.ch[3];emit(&s,3,s.base+ch->ring+(ch->completed%ch->slots)*16,400,2);assert(ipa_gsi_event(&s,3,&buf,&n)==1);assert(n==400 && buf>=0x40000 && buf<0xc0000);assert(ipa_gsi_post(&s,3,8192,0,0));
 }
 regs[0x250a8/4]=0x1000f;assert(ipa_gsi_stop(&s));assert(regs[0x250a8/4]==0x10000);for(unsigned i=0;i<4;i++)assert(!s.ch[i].channel_owned && !s.ch[i].event_owned);
 reset();regs[ipa_gch(20,0)/4]=1<<20;assert(!ipa_gsi_setup(&s,io,dma,0x20000000));assert(!writes);
 for(unsigned j=1;j<=8;j++){reset();fail_command=j;assert(!ipa_gsi_setup(&s,io,dma,0x20000000));assert(s.uncertain);unsigned before=writes;assert(!ipa_gsi_stop(&s));assert(before==writes);}
 reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,3));assert(ipa_gsi_post(&s,3,8192,0,0));emit(&s,3,s.base+s.ch[3].ring+16,10,2);assert(ipa_gsi_event(&s,3,&buf,&n)<0 && s.ch[3].completed==0);assert(ipa_gsi_stop(&s));
 reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,0));fail_command=commands+1;assert(!ipa_gsi_stop(&s));assert(s.uncertain && s.ch[0].channel_owned);
 reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,3));assert(ipa_gsi_post(&s,3,8192,0,0));emit(&s,3,s.base+s.ch[3].ring,8193,2);assert(ipa_gsi_event(&s,3,&buf,&n)<0);assert(ipa_gsi_stop(&s));
 reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,0));assert(ipa_gsi_post(&s,0,7,payload,sizeof(payload)));emit(&s,0,s.base+s.ch[0].ring,0,0x11);assert(ipa_gsi_event(&s,0,&buf,&n)<0);assert(ipa_gsi_stop(&s));
 for(unsigned nth=1;nth<=4;nth++){
  reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));for(unsigned i=0;i<4;i++)assert(ipa_gsi_start(&s,i));
  fail_command=commands+nth;assert(!ipa_gsi_quiesce(&s));
  for(unsigned i=0;i<4;i++)assert(s.ch[i].channel_owned && s.ch[i].event_owned);
  unsigned before=writes;assert(!ipa_gsi_stop(&s) && writes==before);
 }
 /* Runtime faults must be observed with no completion and before a doorbell. */
 const unsigned faults[][2]={{0x25240,0x02301000},{0x2520c,2},{0x25200,1}};
 for(unsigned k=0;k<3;k++){
  reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,2));
  regs[faults[k][0]/4]=faults[k][1];unsigned before=writes;
  assert(ipa_gsi_event(&s,2,&buf,&n)==-1 && s.failed);
  assert(!ipa_gsi_post(&s,2,8,payload,8) && writes==before && !s.ch[2].posted);
  regs[faults[k][0]/4]=0;assert(s.failed);assert(ipa_gsi_stop(&s));
  reset();assert(ipa_gsi_setup(&s,io,dma,0x20000000));assert(ipa_gsi_start(&s,2));
  fault_sync_offset=s.ch[2].ring;fault_register=faults[k][0];fault_value=faults[k][1];before=writes;
  assert(!ipa_gsi_post(&s,2,8,payload,8));assert(s.failed && !s.ch[2].posted && writes==before);
  fail_command=commands+1;assert(!ipa_gsi_stop(&s) && s.uncertain && s.ch[2].channel_owned);
  before=writes;assert(!ipa_gsi_stop(&s) && writes==before);
 }
 /* ast179 raw log contents are not pending status before initialization. */
 reset();regs[0x25240/4]=0x83c7ffe1;assert(ipa_gsi_setup(&s,io,dma,0x20000000));
 assert(error_inits==1 && !regs[0x25240/4]);assert(ipa_gsi_stop(&s));
 for(unsigned global=1;global<=2;global++){
  reset();regs[0x25240/4]=0x83c7ffe1;regs[0x25200/4]=global;
  assert(!ipa_gsi_setup(&s,io,dma,0x20000000));assert(!writes && !error_inits);
 }
 reset();regs[0x25240/4]=0x83c7ffe1;drop_at=6;
 assert(!ipa_gsi_setup(&s,io,dma,0x20000000));assert(s.failed && !commands && !error_inits && regs[0x25240/4]==0x83c7ffe1);assert(ipa_gsi_stop(&s));
 reset();regs[0x25240/4]=0x83c7ffe1;init_fault=true;
 assert(!ipa_gsi_setup(&s,io,dma,0x20000000));assert(s.failed && !commands && error_inits==1 && regs[0x25200/4]==1);assert(ipa_gsi_stop(&s));
 /* A pending fault arriving during the final context read keeps its log. */
 for(unsigned k=0;k<2;k++){
  reset();regs[0x25040/4]=0xe4046312;regs[0x25050/4]=0x2011;regs[0x25240/4]=0x83c7ffe1;
  survey_fault_register=k?0x2520c:0x25200;survey_fault_value=k?2:1;
  assert(!ipa_gsi_setup(&s,io,dma,0x20000000));
  assert(s.failed && !error_inits && !commands && regs[0x25240/4]==0x02301000);
  assert(s.error_log==0x02301000 && (k?s.irq_status==2:s.global_status==1));
  assert(ipa_gsi_stop(&s));assert(regs[0x25240/4]==0x02301000 && !error_inits);
 }
 puts("IPA GSI5.2 layout, ring wrap, event validation, command timeout and DMA stop PASS");return 0;
}
