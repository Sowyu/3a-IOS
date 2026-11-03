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
code=prefix+f('static int m_scm(', 'static int m_assign(')+rmt+f('static void m_shutdown_receive(', '/* Insert before m_greceive')+f('static bool m_shutdown_request(', 'static int m_shadow_prepare(')+f('static bool m_cleanup(', 'static int m_power_inspect(')+(here/'tests/lifecycle-tail.c').read_text().split('int main(void)')[0]+(here/'tests/shutdown-joined-tail.c').read_text()
(out/'shutdown-joined.c').write_text(code)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=c11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'shutdown-joined.c'),str(out/'QcomQrtr.c'),str(out/'ModemRmtfs.c'),'-o',str(out/'check-shutdown-joined')],env=env,check=True)
subprocess.run([str(out/'check-shutdown-joined')],env=env,check=True)
