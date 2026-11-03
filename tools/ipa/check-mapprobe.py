"""Build host fixtures from the actual worker gate and APPS allocator."""
from pathlib import Path
import subprocess as sp,os
H=Path(__file__).resolve().parent;O=H/'tests/out';O.mkdir(exist_ok=True)
# check-datapath.sh reconstructs this file with every additive IPA hunk.
s=(O/'ModemRuntime.inc').read_text();a=s.index('    memset(&m,0,sizeof(m));',s.index('static int modem_run('));b=s.index('    m_sleep(30000);',a)
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "ModemGate.h"
static const char *args;
static const char *PE_boot_args(void){return args;}
static bool m_gate(const char *s){return modem_gate(args,s);}
static struct {int master;bool glink,rmtfs,pdm,uim,uim_ready_poll,data_session,sysmon,bcm_probe,handover_release,bandwidth,bandwidth_irq;}m;
static struct {bool enabled,map_probe;}ipa;
static bool ipa_full;static int ipa_smp_index,ipa_qmi_logged;
static int gate(void){
'''
tail=r'''
return 0;}
int main(void){
 const char *base="sweetmpss=1 sweetmbw=1 sweetmbwi=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=600";
 char text[1024];snprintf(text,sizeof(text),"%s sweetipamap=1",base);args=text;assert(!gate() && ipa.map_probe && ipa.enabled && !ipa_full);
 const char *bad[]={"sweetipa=1","sweetipafw=1","sweetmglink=1","sweetmuim=1","sweetmstop=1","sweetmbcm=1","sweetmho=1","sweetdata=1","sweetqmiraw=1","sweetmsession=1","sweetmrmtfs=1","sweetmpdm=1","sweetmready=1","sweetdeadman=900","sweetfbarm=0","sweetselftest=0","sweetmpss=0","sweetmbw=0","sweetmbwi=0"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){snprintf(text,sizeof(text),"%s sweetipamap=1 %s",base,bad[i]);assert(gate()==EINVAL);}
 const char *off[]={"","sweetipamap","sweetipamap=0","sweetipamap=01","sweetipamap=1 sweetipamap=1"};
 for(unsigned i=0;i<sizeof(off)/sizeof(*off);i++){snprintf(text,sizeof(text),"%s %s",base,off[i]);assert(!gate() && !ipa.map_probe && !ipa.enabled);}
 puts("PASS actual SMMU-only worker gates: dependencies, conflicts, default-off and exact values");return 0;
}
'''
(O/'mapgate.c').write_text(prefix+s[a:b]+tail)
# Read the actual generic allocator from the independently reproduced build tree.
f=H.parents[2]/'ios-asteroids/research/xnu-ipa/dxb/xnu/bsd/dev/sweet_smmu.c'
s=f.read_text();a=s.index('\tuint32_t id0 =',s.index('smmu_bank_create(void)'));b=s.index('\n\t/* identity map',a)
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
static uint32_t regs[0x100000/4];static int smmu_cb=-1;static unsigned smmu_nsmr;
static uint32_t smmu_rd(unsigned a){assert(a<sizeof(regs));return regs[a/4];}
static int choose(void){
'''
tail=r'''
return smmu_cb;}
int main(void){
 regs[0x20/4]=0x4c017e72;regs[0x24/4]=0x60000048;
 for(unsigned i=0;i<72;i++)regs[(0x80000+4096*i)/4]=1;
 regs[(0x80000+4096*5)/4]=0;regs[(0x1000+4*5)/4]=0x1f301;regs[(0x1800+4*5)/4]=1;
 regs[(0x80000+4096*6)/4]=0;regs[(0x1800+4*6)/4]=1;
 regs[(0x80000+4096*7)/4]=0;
 assert(choose()==7);puts("PASS actual generic APPS allocator skips parked/private-format banks");return 0;
}
'''
(O/'mapallocator.c').write_text(prefix+s[a:b]+tail)
for n in ['mapgate','mapallocator']:
 sp.run(['cc','-save-temps=obj','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(O),str(O/(n+'.c')),'-o',str(O/n)],check=True)
 sp.run([str(O/n)],check=True)
