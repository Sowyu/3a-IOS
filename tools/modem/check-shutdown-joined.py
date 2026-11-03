#!/usr/bin/env python3
"""Compile the joined production shutdown path; no phone access."""
from pathlib import Path
import os,subprocess
here=Path(__file__).resolve().parent;out=here/'tests/out'
prefix=(here/'tests/lifecycle-prefix.c').read_text()
prefix=prefix.replace('#include "QcomScm.h"','#include "QcomScm.h"\n#include "QcomQrtr.h"\n#include "ModemRmtfs.h"')
prefix=prefix.replace('bool failed,fatal,xo_pinned,','bool sysmon,qr_started,shutdown_sent,shutdown_response,shutdown_ind;uint32_t shutdown_node,shutdown_port;bool failed,fatal,xo_pinned,')
prefix=prefix.replace('struct{uint32_t remote_count;}qr;','QcomQrtr qr;').replace('struct{void *image[4];uint32_t bytes[4];}fs;','ModemRmtfs fs;')
prefix=prefix.replace('static bool m_shutdown_request(void){return graceful;}\n','')
prefix=prefix.replace('if(m.started)assert(stop_updates==(graceful?0u:2u));','if(m.started)assert(stop_updates==((m.shutdown_response && m.shutdown_ind)?0u:2u));')
prefix += 'static uint16_t le16(const uint8_t *p);\nstatic void m_ipc_poll(void);\n'
s=(out/'ModemRuntime.inc').read_text()
def f(start,end):return s[s.index(start):s.index(end)]
# Hardware barriers have no meaning in this single-threaded host transport.
rmt=f('static void m_rmtfs_receive(', 'static void m_pd_receive(').replace('__asm__ volatile("dmb sy":::"memory");','').replace('__asm__ volatile("dsb sy":::"memory");','')
code=prefix+f('static int m_scm(', 'static int m_assign(')+rmt+f('static void m_shutdown_receive(', 'static void m_uim_readiness(')+f('static bool m_shutdown_request(', 'static int m_shadow_prepare(')+f('static bool m_cleanup(', 'static int m_power_inspect(')+(here/'tests/lifecycle-tail.c').read_text().split('int main(void)')[0]+(here/'tests/shutdown-joined-tail.c').read_text()
(out/'shutdown-joined.c').write_text(code)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'shutdown-joined.c'),str(out/'QcomQrtr.c'),str(out/'ModemRmtfs.c'),'-o',str(out/'check-shutdown-joined')],env=env,check=True)
subprocess.run([str(out/'check-shutdown-joined')],env=env,check=True)

# Join actual handover/RSC functions to the same real shutdown transport.
import re
vote=(here/'tests/vote-prefix.c').read_text()
vote=vote[vote.index('static uint32_t reg['):vote.index('static void sweet_log_sync(')]
vote=re.sub(r'\bmode\b','vote_mode',vote)
joined_prefix=prefix.replace('bool sysmon,','uintptr_t rsc;uint32_t rail_addr[2],rail_index[2],xo_addr;bool handover_release;bool sysmon,')
joined_prefix=joined_prefix.replace('static int m_vote(bool b){assert(!b);poweroffs++;return 0;}\n','')
joined_tail=(here/'tests/shutdown-joined-tail.c').read_text().split('int main(void)')[0]
handover_tests=r"""
static void handover_init(void){
 joined_init();ipc_mode=1;memset(reg,0,sizeof(reg));vote_mode=locked=lockcalls=triggers=xowrites=0;
 m.rsc=1;m.rail_addr[0]=0x100;m.rail_addr[1]=0x200;m.rail_index[0]=3;m.rail_index[1]=4;m.xo_addr=0x300;reg[0xd00/4]=5;
 assert(!m_vote(true));m.handover_release=true;m.state=6;
}
int main(void){
 handover_init();assert(!m_handover() && !m.votes_owed && triggers==2);
 assert(m_cleanup());assert(shutdowns==1 && triggers==2 && unmaps==4 && frees==1 && m.fs.writes==1);
 handover_init();vote_mode=3;assert(m_handover()==ETIMEDOUT && m.failed && m.vote_uncertain && m.votes_owed);
 assert(!m_cleanup());assert(shutdowns==1 && triggers==2 && !unmaps && !frees && reclaims==1 && !m.fs.writes && stop_updates==2);
 handover_init();vote_mode=3;assert(m_handover()==ETIMEDOUT);shut_status=-3145800;
 assert(!m_cleanup());assert(shutdowns==50 && triggers==2 && !reclaims && !unmaps && !frees);
 handover_init();vote_mode=2;assert(m_handover()==ENODEV && m.failed && !m.vote_uncertain && m.votes_owed && triggers==1);
 vote_mode=0;assert(!m_cleanup());assert(shutdowns==1 && triggers==2 && unmaps==4 && frees==1);
 handover_init();m.failed=true;assert(!m_handover() && triggers==1 && m.votes_owed);
 handover_init();m.vote_uncertain=true;assert(!m_handover() && triggers==1 && m.votes_owed);
 puts("PASS: joined actual handover/RSC/SSCTL/RAM-rmtfs/PAS cleanup, timeout retention and ENODEV failure");
}
"""
code=joined_prefix+vote+f('static int m_vote(', 'static int m_qwait(')+f('static int m_scm(', 'static int m_assign(')+rmt+f('static void m_shutdown_receive(', 'static void m_uim_readiness(')+f('static bool m_shutdown_request(', 'static int m_shadow_prepare(')+f('static bool m_cleanup(', 'static int m_power_inspect(')+(here/'tests/lifecycle-tail.c').read_text().split('int main(void)')[0]+joined_tail+handover_tests
(out/'handover-joined.c').write_text(code)
subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'handover-joined.c'),str(out/'QcomQrtr.c'),str(out/'ModemRmtfs.c'),'-o',str(out/'check-handover-joined')],env=env,check=True)
subprocess.run([str(out/'check-handover-joined')],env=env,check=True)
