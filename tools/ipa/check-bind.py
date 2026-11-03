from pathlib import Path
import subprocess as sp
D=Path(__file__).resolve().parent/'tests/out';A=D/'adapter';M=A/'bsd/dev/modem';F=A/'bsd/dev/data';I=A/'inc/libkern'
for p in [M,F,I]:p.mkdir(parents=True,exist_ok=True)
(I/'libkern.h').write_text('#include <stdio.h>\n')
extract=Path(__file__).resolve().parent.parent/'modem/extract.py'
for name in ['sweet_data_ipa.c','sweet_data_ipa.h','sweet_data.h']:
 sp.run(['python3',str(extract),str(Path(__file__).resolve().parents[2]/'patches/data'),str(F/name),'bsd/dev/data/'+name],check=True)
for name in ['SweetIpa.h','QcomQrtr.h']:
 (M/name).write_bytes((D/name).read_bytes())
s=(D/'ModemRuntime.inc').read_text();start=s.index('    if(ipa.enabled){',s.index('static int modem_run('));end=s.index('    if((rc=m_pas(5,1,0,0)))',start)
prefix=r'''
#define ARM64_BOARD_CONFIG_SWEET 1
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "adapter/bsd/dev/data/sweet_data_ipa.c"
struct wl_fat {int unused;};
static struct {bool enabled;}ipa;
static struct {bool data_session,data_attached,data_ipa_bound;QcomQrtr qr;}m;
static bool ipa_full,data_gate;static int ipa_smp_index;
static int phase,prepare_rc,hardware_rc,set_rc,rx_rc,attach_rc,load_rc;
static unsigned sets,regs,attaches,loads,refreshes;
static bool m_gate(const char *s){assert(!strcmp(s,"sweetdata"));return data_gate;}
static int ipa_prepare(const struct wl_fat *f){assert(f && phase==0);phase=1;return prepare_rc;}
static int ipa_hardware_setup(void){assert(phase==1);phase=2;return hardware_rc;}
int sweet_data_set_ipa(int (*tx)(void *,const uint8_t *,uint32_t),void *ctx,int version,uint32_t bytes,uint32_t count){
 assert(phase==2 && tx && !ctx && version==1 && bytes==8192 && count==16);sets++;return set_rc;
}
int sweet_ipa_register_rx(sweet_ipa_rx_fn fn,void *ctx){assert(phase==2 && sets && fn && !ctx);regs++;if(!rx_rc)phase=3;return rx_rc;}
int sweet_data_attach(QcomQrtr *qr){assert(qr==&m.qr && m.data_attached);assert(data_gate?(phase==3 && m.data_ipa_bound):!m.data_ipa_bound);attaches++;phase=4;return attach_rc;}
static int m_load(const struct wl_fat *f,const uint8_t *p,uint32_t n){assert(f && p && n==1 && phase==4);loads++;return load_rc;}
int sweet_ipa_transmit(const void *p,size_t n){(void)p;(void)n;return 0;}
void sweet_data_ipa_rx(const uint8_t *p,uint32_t n){(void)p;(void)n;}
bool sweet_ipa_ready(void){return false;}
void sweet_data_ipa_ready(int value){assert(!value);refreshes++;}
static int run(void){struct wl_fat fat={0};const struct wl_fat *f=&fat;uint8_t byte=0;const uint8_t *mdt=&byte;uint32_t size=1;int rc=0;
'''
tail=r'''
return 0;out:return rc;}
static void reset(void){memset(&m,0,sizeof(m));m.data_session=ipa_full=data_gate=ipa.enabled=true;ipa_smp_index=1;phase=prepare_rc=hardware_rc=set_rc=rx_rc=attach_rc=load_rc=0;sets=regs=attaches=loads=refreshes=0;}
int main(void){
 reset();assert(!run() && sets==1 && regs==1 && attaches==1 && loads==1 && m.data_ipa_bound);
 reset();rx_rc=EBUSY;assert(run()==EBUSY && sets==1 && regs==1 && !attaches && !loads && !refreshes && !m.data_ipa_bound);
 reset();set_rc=-1;assert(run()==EPERM && sets==1 && !regs && !attaches && !loads && !m.data_ipa_bound);
 reset();prepare_rc=EIO;assert(run()==EIO && !sets && !attaches && !loads);
 reset();hardware_rc=EIO;assert(run()==EIO && !sets && !attaches && !loads);
 reset();attach_rc=EIO;assert(run()==EIO && attaches==1 && !loads && m.data_attached);
 reset();data_gate=false;assert(!run() && !sets && !regs && attaches==1 && loads==1 && !m.data_ipa_bound);
 puts("PASS: actual worker binding order and real2221 partial-bind failures remain terminal before attach/MPSS");return 0;
}
'''
(D/'binding.c').write_text(prefix+s[start:end]+tail)
