from pathlib import Path
here=Path(__file__).resolve().parent;d=here/'tests/out'
s=(d/'ModemRuntime.inc').read_text();data=(d/'sweet_data.c').read_text()
t=(here/'../data/tests/services.c').read_text()
prefix=r'''
#include <stdbool.h>
#define LOAD(x) (x)
#include "QcomGlink.h"
static struct {QcomQrtr qr; bool uim_ready,data_stopping,failed,fatal,data_attached; uint64_t uim_seen; uint32_t uim_port;} m;
static struct {int attached,session_on,usim,ipa_ready,ready_given; void *ipa_tx; unsigned qmap,ipa_datagrams,ipa_rx_bytes; QcomQrtr *qr; DataServices services;CellSession session;} sd;
static uint64_t m_now(void){return 0;}
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void sweet_data_usim(int ready){sd.usim=ready;}
static bool m_uim_endpoint_valid(void){assert(!m.uim_port);return true;}
static void sd_service_lost(void *ctx,uint8_t service){(void)ctx;(void)service;}
static void sd_readiness(void);
'''
helper=s[s.index('static void m_uim_readiness('):s.index('static bool m_uim_endpoint_valid(')]
sync=data[data.index('static void\nsd_sync_services('):data.index('static void\nsd_raw_poll(')]
# sd_readiness and discovery are copied verbatim. No kernel raw waiter is exercised.
receive=s[s.index('static void m_greceive('):s.index('static void m_gstate(')]
t=t.replace('static QcomQrtr q;\nstatic DataServices svcs;\nstatic CellSession s;',prefix+helper+sync+receive+'\n#define q m.qr\n#define svcs sd.services\n#define s sd.session')
t=t.replace('assert(qcom_qrtr_receive(&q, p, 32 + ((n + 3) & ~3u)) == 0);','m_greceive(0,0,p,32+((n+3)&~3u));')
t=t.replace('static void discovery(void) {data_services_sync(&svcs, &q, &s, 0, 0);}','')
t=t.replace('if (hook) discovery();  /* the modem runtime calls sweet_data_discovery after each control packet */','(void)hook; /* actual modem callback owns the hook */')
t=t.replace('memset(&out, 0, sizeof out);','memset(&out, 0, sizeof out);\n    memset(&m,0,sizeof m);memset(&sd,0,sizeof sd);m.data_attached=true;sd.attached=sd.session_on=1;sd.qr=&q;')
t=t.replace('cell_session_ready(&s, &ready);','sd.usim=sd.ipa_ready=sd.ready_given=1;sd.ipa_tx=&sd;sd.qmap=QMAP_V1;sd.ipa_datagrams=16;sd.ipa_rx_bytes=8192;cell_session_ready(&s, &ready);')
t=t.replace('data service cache checks: PASS','PASS: actual modem receive and data discovery hooks preserve DEL/NEW loss before real session replies')
(d/'discovery.c').write_text(t)
