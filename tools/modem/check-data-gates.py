from pathlib import Path
p=Path(__file__).resolve().parent/'tests/out';s=(p/'ModemRuntime.inc').read_text()
a=s.index('    memset(&m,0,sizeof(m));m.master=-1;');b=s.index('    m_sleep(30000);',a)
prefix=r'''
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "ModemGate.h"
static const char *args;
#define PE_boot_args() args
static bool m_gate(const char *name){return modem_gate(args,name)!=0;}
static struct {int master;bool glink,rmtfs,pdm,uim,uim_ready_poll,data_session,sysmon,bcm_probe,handover_release,bandwidth,bandwidth_irq;}m;
static int admit(void){
'''
tail=r'''
return 0;}
int main(void){
 const char *gates[]={"sweetmsession=1","sweetdata=1","sweetmpss=1","sweetmuim=1","sweetmready=1","sweetmglink=1","sweetmrmtfs=1","sweetmpdm=1","sweetmstop=1","sweetmho=1","sweetfbarm=1","sweetselftest=1","sweetdeadman=900"};
 char line[1024];
 for(unsigned skip=0;skip<=13;skip++){
  line[0]=0;for(unsigned i=0;i<13;i++)if(i!=skip){strcat(line,gates[i]);strcat(line," ");}
  args=line;assert(admit()==(skip==13?0:EINVAL));
 }
 args="";assert(!admit());args="sweetdata=1";assert(admit()==EINVAL);args="sweetmsession=1";assert(admit()==EINVAL);
 line[0]=0;for(unsigned i=0;i<13;i++){strcat(line,i==1?"sweetqmiraw=1":gates[i]);strcat(line," ");}
 args=line;assert(!admit());strcat(line,"sweetdeadman=600");assert(admit()==EINVAL);
 puts("PASS: actual pre-hardware session admission rejects missing dependencies, missing data/raw request and non-exact 900s guard");
}
'''
(p/'gates.c').write_text(prefix+s[a:b]+tail)
