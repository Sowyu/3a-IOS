#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
static struct {uintptr_t rsc;uint32_t rail_addr[2],rail_index[2],xo_addr;uint32_t state;bool started,handover_release,failed,fatal,votes_owed,vote_uncertain,xo_pinned;}m;
static uint32_t reg[0x4000/4];static unsigned locked,lockcalls,mode,triggers,xowrites;
static void sweet_apps_rsc_lock(void){assert(!locked);locked=1;lockcalls++;}
static void sweet_apps_rsc_unlock(void){assert(locked);locked=0;}
static uint32_t m_rd(uintptr_t p,unsigned o){(void)p;assert(locked && o<sizeof(reg));
 if(o==0xd28)return mode==2?0:1;
 if(mode==3 && o==0xd40)return 0;
 if(o==0xd40 || o==0xd58 || o==0xd70)return mode==1 && o==0xd70?0:0x10100;
 return reg[o/4];
}
static void m_wr(uintptr_t p,unsigned o,uint32_t v){(void)p;assert(locked && o<sizeof(reg));reg[o/4]=v;
 if(o==0xd24 && (v&(1u<<24))){triggers++;if(triggers==2){assert(reg[0xd2c/4]==1 && reg[0xd38/4]==m.rail_addr[1] && !reg[0xd3c/4]);}for(unsigned i=0;i<3;i++)if((reg[0xd2c/4]&(1u<<i)) && reg[(0xd38+i*24)/4]==m.xo_addr){assert(reg[(0xd3c+i*24)/4]==3);xowrites++;}}
}
static void delay(unsigned us){assert(us==1);}

static void sweet_log_sync(void){}
