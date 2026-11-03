#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "ModemBcm.h"
static struct {bool bandwidth,bandwidth_pinned,vote_uncertain,failed;uintptr_t rsc,db;}m;
static uint8_t db[0x20000];
static uint32_t reg[0x4000/4];
static uint32_t effective_sn3;
static unsigned mode,locked,triggers,reads,writes,delays;
static void sweet_apps_rsc_lock(void){assert(!locked);locked=1;}
static void sweet_apps_rsc_unlock(void){assert(locked);locked=0;}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t m_rd(uintptr_t p,unsigned o){
 reads++;
 if(p==2){assert(o<=sizeof(db)-4);return get32(db+o);}
 assert(p==1 && o<sizeof(reg));if(o!=0xc)assert(locked);
 return reg[o/4];
}
static void m_wr(uintptr_t p,unsigned o,uint32_t v){
 assert(p==1 && locked && o<sizeof(reg));writes++;
 if(o==0xd08){reg[0xd04/4]&=~v;return;}
 reg[o/4]=v;
 for(unsigned t=0;t<3;t++){
  unsigned b=0xd00+t*0x2a0;
  if(o==b+0x24 && (v&(1u<<24))){
   triggers++;uint32_t mask=reg[(b+0x2c)/4];assert(mask==1 || mask==0x7f);
   assert(reg[(b+0x20)/4]==(mask==1?1:0x75));
   for(unsigned i=0;i<7;i++)if((mask&(1u<<i)) && reg[(b+0x38+i*24)/4]==0x5001c)effective_sn3=reg[(b+0x3c+i*24)/4];
   if(mode!=1)reg[0xd04/4]|=1u<<t;
   for(unsigned i=0;i<7;i++)reg[(b+0x40+i*24)/4]=(mode==2 && i==3)?0x100:0x10100;
  }
 }
}
static void delay(unsigned us){assert(us==1);delays++;}
static void sweet_log_sync(void){}
static void w16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void w32(uint8_t *p,uint32_t v){w16(p,v);w16(p+2,v>>16);}
static void init(void){
 memset(&m,0,sizeof(m));memset(db,0,sizeof(db));memset(reg,0,sizeof(reg));mode=locked=triggers=reads=writes=delays=effective_sn3=0;
 m.bandwidth=true;m.rsc=1;m.db=2;reg[0xc/4]=16u<<27;reg[0xd00/4]=5;
 for(unsigned t=0;t<3;t++)reg[(0xd00+t*0x2a0+0x28)/4]=1;
 static const uint32_t addr[]={0x50034,0x5001c,0x50010,0x50008,0x50004,0x50000,0x50068};
 static const uint32_t units[]={7935,530000,530000,4200000,4200000,1600000,1};
 static const unsigned widths[]={8,8,16,32,32,8,0},vcds[]={6,3,3,1,1,0,12};
 w32(db+4,QCOM_CMD_DB_MAGIC);w16(db+8,5);w16(db+12,168);w16(db+14,7);w16(db+16,0x1000);
 for(unsigned i=0;i<7;i++){
  uint8_t *e=db+144+24*i,*a=db+312+8*i;memcpy(e,modem_bcm_names[i],strlen(modem_bcm_names[i]));w32(e+16,addr[i]);w16(e+20,8);w16(e+22,8*i);
  w32(a,units[i]);w16(a+4,widths[i]);a[6]=vcds[i];
 }
}
