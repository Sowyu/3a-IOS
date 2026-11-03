from pathlib import Path
p=Path(__file__).resolve().parent/'tests/out';s=(p/'ModemRuntime.inc').read_text()
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "ModemGate.h"
static struct {bool data_attached,data_stopping,started,glink,uim_ready,failed,fatal;void *channel;int qr;} m;
static unsigned stops,ipc,polls,smp,detach,revoke,finish_after;static uint64_t now;static int cleanup;
static bool quiesced,quiesce_fail,ipa_ready;
static void ipa_transport_sync(void){assert(m.data_stopping);ipa_ready=false;}
static bool ipa_data_quiesce(void){if(m.data_attached)assert(!detach);if(m.data_attached)assert(stops && !ipa_ready);ipa_ready=false;quiesced=true;return !quiesce_fail;}
static void sweet_data_stop(void){assert(!stops && !detach && !m.uim_ready && m.data_stopping);stops++;}
static uint64_t m_now(void){return now;}
static void m_smp_poll(void){assert(stops && !detach);smp++;}
static void m_ipc_poll(void){assert(stops && !detach);ipc++;}
static void sweet_data_poll(int *q,uint64_t at){assert(stops && !detach && !ipa_ready && q==&m.qr && at==now);polls++;}
static int sweet_data_idle(void){return polls>=finish_after;}
static int sweet_data_cleanup(void){assert(stops && !detach);return cleanup;}
static void m_sleep(unsigned ms){assert(ms==2);now+=ms;}
static void sweet_data_usim(int ready){assert(!ready && !detach);revoke++;}
static void sweet_data_detach(void){assert(stops && !m.uim_ready && !detach && quiesced && !ipa_ready);detach++;}
static void init(void){memset(&m,0,sizeof(m));stops=ipc=polls=smp=detach=revoke=0;now=0;cleanup=0;quiesced=quiesce_fail=false;ipa_ready=true;finish_after=3;m.data_attached=m.started=m.glink=m.uim_ready=true;m.channel=&m;}
'''
helper=s[s.index('static void m_uim_readiness('):s.index('static void m_uim_refresh(')]
code=s[s.index('static bool m_data_stop('):s.index('static bool m_cleanup(')]
tail=r'''
int main(void){
 init();m.data_attached=false;assert(m_data_stop() && !stops && !polls && !detach && quiesced);
 init();m.data_attached=false;quiesce_fail=true;assert(!m_data_stop() && m.failed && !detach);
 init();assert(m_data_stop() && stops==1 && polls==3 && ipc==3 && smp==3 && detach==1 && !m.data_attached && !m.uim_ready && !m.failed && revoke==1);
 assert(m_data_stop() && stops==1 && detach==1);
 init();finish_after=3000;assert(!m_data_stop() && now==5000 && polls==2500 && detach==1 && m.failed);
 init();m.started=m.glink=false;m.channel=0;assert(m_data_stop() && !ipc && !smp && detach==1);
 for(unsigned e=1;e<100;e++){init();cleanup=e;assert(!m_data_stop() && m.failed && detach==1);}
 init();quiesce_fail=true;assert(!m_data_stop() && m.failed && detach==1 && quiesced && !ipa_ready);
 const char *bad[]={"","sweetdeadman","sweetdeadman=0900","sweetdeadman=9000","sweetdeadman=900 sweetdeadman=900","sweetdeadman=900 sweetdeadman=600","sweetdeadman=600 sweetdeadman=900"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!modem_arg_is(bad[i],"sweetdeadman","900"));
 assert(modem_arg_is("x=1 sweetdeadman=900 y=2","sweetdeadman","900"));
 assert(modem_gate("sweetmsession=1","sweetmsession"));assert(!modem_gate("sweetmsession","sweetmsession"));
 puts("PASS: actual data teardown revokes, pumps, bounds, checks cleanup outcome, detaches once and preserves exact session guard");return 0;
}
'''
(p/'worker.c').write_text(prefix+helper+code+tail)
