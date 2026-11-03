#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
static struct {uintptr_t rsc,smem;struct {volatile uint32_t *in;} sp;uint32_t rail_addr[2],rail_index[2],xo_addr;uint32_t state;bool started,handover_release,failed,fatal,votes_owed,vote_uncertain,xo_pinned,proxy_released;}m;
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
 if(o==0xd24 && (v&(1u<<24))){triggers++;if(triggers==2){assert(reg[0xd2c/4]==3 && reg[0xd38/4]==m.rail_addr[0] && !reg[0xd3c/4] && reg[0xd50/4]==m.rail_addr[1] && !reg[0xd54/4]);}for(unsigned i=0;i<3;i++)if((reg[0xd2c/4]&(1u<<i)) && reg[(0xd38+i*24)/4]==m.xo_addr){assert(reg[(0xd3c+i*24)/4]==3);xowrites++;}}
}
static void delay(unsigned us){assert(us==1);}

static void sweet_log_sync(void){}

#define QCOM_SMP2P_ITEM_BYTES 340
static unsigned remote_state;
static uint32_t dummy[85];
static int m_item(unsigned id,unsigned size,uint32_t *off,uint32_t *bytes){assert(id==443 && size==340);*off=0;*bytes=340;return 0;}
static int qcom_smp2p_service(void *sp,const char *name,uint32_t *value,uint32_t *changed){(void)sp;assert(!strcmp(name,"slave-kernel"));*value=remote_state;*changed=remote_state;return 0;}
static void m_ring(unsigned client,unsigned signal){assert(client==3 && signal==2);}
