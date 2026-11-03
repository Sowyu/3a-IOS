#!/usr/bin/env python3
"""Check the exact production bandwidth encoder and RSC transaction on host."""
from pathlib import Path
import os,subprocess
here=Path(__file__).resolve().parent;out=here/'tests/out';s=(out/'ModemRuntime.inc').read_text()
touch=(here.parents[2]/'ios-asteroids/research/xnu-modem/dxb/xnu/bsd/dev/sweet_touch_asteroids.c').read_text()
def part(a,b):return touch[touch.index(a):touch.index(b)]
bridge=r"""
#include "ModemGate.h"
static struct {uintptr_t rsc;}at={1};
static const char *boot_args;
static const char *PE_boot_args(void){return boot_args;}
static uint32_t at_rd(uintptr_t p,int32_t o){return m_rd(p,(unsigned)o);}
static void at_wr(uintptr_t p,int32_t o,uint32_t v){m_wr(p,(unsigned)o,v);}
static uint32_t at_db_lookup(const char *name,unsigned slave){assert(slave==5);return !strcmp(name,"SN3")?0x5001c:0x50038;}
#define SLAVE_BCM 5u
"""
touch_code=bridge+part('#define TCS_BASE','#define DB_MAGIC')+part('static uint32_t\ntcs_rd(', '/* Shared with the modem:')+part('static int at_amc(', '/* VRM:')+part('static int\nat_bcm(', '/*\n * Memory bandwidth')
code=(here/'tests/bandwidth-prefix.c').read_text()+touch_code+s[s.index('static int m_bandwidth_vote('):s.index('static int m_vote(')]+(here/'tests/bandwidth-tail.c').read_text()
(out/'bandwidth.c').write_text(code)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'bandwidth.c'),str(out/'QcomRpmh.c'),'-o',str(out/'check-bandwidth')],env=env,check=True)
subprocess.run([str(out/'check-bandwidth')],env=env,check=True)
assert s.index('if((rc=m_bandwidth_vote()))goto out;')<s.index('if((rc=m_load(f,mdt,size)))goto out;')
assert 'm.bandwidth=m_gate("sweetmbw")' in s
