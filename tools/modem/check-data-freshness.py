from pathlib import Path
p=Path(__file__).resolve().parent/'tests/out';s=(p/'ModemRuntime.inc').read_text()
helper=s[s.index('static void m_uim_readiness('):s.index('static bool m_uim_endpoint_valid(')]
a=s.index('m_uim_refresh();',s.index('while(m_now()<end){m_smp_poll();if((rc=m_handover()))'))
b=s.index('        if(m.fatal || m.failed){rc=EIO;break;}',a)
step=s[a:b]
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "CellSession.h"
static struct {bool uim_ready,data_stopping,failed,fatal,data_attached,uim,glink;uint64_t uim_seen;unsigned state;int qr;} m;
static uint64_t now;
static CellSession session;
static unsigned sends,polls,card_polls,ipc;
static int sent(void *ctx,unsigned client,uint8_t service,const uint8_t *msg,uint32_t len){(void)ctx;(void)client;(void)service;(void)msg;(void)len;sends++;return 0;}
static uint64_t m_now(void){return now;}
static void sweet_data_usim(int ready){if(ready){const CellReady r={QMAP_V1,16,8192};cell_session_ready(&session,&r);}else cell_session_ready(&session,0);}
static void sweet_data_poll(int *qr,uint64_t at){assert(qr==&m.qr && at==now);if(now>=15000)assert(!m.uim_ready && !session.is_ready);polls++;cell_session_poll(&session,at);}
static void m_uim_poll(void){card_polls++;}
static void m_ipc_poll(void){ipc++;}
'''
tail=r'''
static void init(void){
 memset(&m,0,sizeof m);now=0;sends=polls=card_polls=ipc=0;m.state=6;m.data_attached=m.uim=m.glink=true;
 const CellConfig cfg={.allow_online=1,.want_v4=1,.want_v6=1,.mux=1,.ep_interface=1};const CellSessionOps ops={sent,0,0};
 cell_session_init(&session,&cfg,&ops,0);
 const uint8_t svcs[]={QMI_SVC_DMS,QMI_SVC_NAS,QMI_SVC_WDS,QMI_SVC_WDA,QMI_SVC_DPM};
 for(unsigned i=0;i<sizeof svcs;i++)cell_session_service(&session,svcs[i],1);
 m_uim_readiness(true);assert(session.state==CS_DPM && session.is_ready);sends=0;
}
int main(void){
 for(unsigned state=0;state<6;state++){
  init();m.state=state;now=14999;worker_step();assert(m.uim_ready && session.is_ready && !card_polls);
  unsigned before=sends;now=15000;worker_step();assert(!m.uim_ready && !session.is_ready && !card_polls && sends==before);
  assert(session.state==CS_FAILED || session.state==CS_STOPPING || session.state==CS_STOPPED);
 }
 init();m.state=4;m.fatal=true;worker_step();assert(!m.uim_ready && !session.is_ready && !sends);
 init();m.state=4;m.failed=true;worker_step();assert(!m.uim_ready && !session.is_ready && !sends);
 puts("PASS: actual worker revokes stale USIM before real session polling with ready/handover cleared and no receive traffic");
}
'''
(p/'freshness.c').write_text(prefix+helper+'static void worker_step(void){'+step+'}\n'+tail)
