#!/usr/bin/env python3
"""Exercise the production worker's QRTR acknowledgment retry ordering."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent;out=root/'tests/out'
s=(out/'ModemRuntime.inc').read_text();code=s[s.index('static void m_ipc_poll('):s.index('static bool m_shutdown_request(')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "QcomQrtr.h"
#define M_TX_COUNT 32u
#define QcomGlinkAgain 1
static struct {uintptr_t smem;struct {struct {void *data;}rx;int broken;}gl;struct {int up;} ch,*channel;QcomQrtr qr;bool qr_started,failed;uint32_t rxoff,qhead,qcount;struct {uint32_t len;uint8_t data[512];}queue[M_TX_COUNT];}m;
static int held,tx_error;static unsigned ack_sent,delivered;
static int m_item(unsigned id,unsigned min,uint32_t *off,uint32_t *bytes){(void)id;(void)min;(void)off;(void)bytes;return -1;}
#define qcom_glink_set_rx(g,d,n) ((void)(g),(void)(d),(void)(n))
#define qcom_glink_poll(g) ((void)(g))
static int mock_send(const void *p,unsigned len){(void)p;(void)len;delivered++;return tx_error?tx_error:held?QcomGlinkAgain:0;}
#define qcom_glink_send(g,c,d,n) ((void)(g),(void)(c),mock_send(d,n))
static int send(void *ctx,const uint8_t *p,uint32_t n){(void)ctx;assert(p[4]==QCOM_QRTR_RESUME_TX && n==52);if(m.qcount==M_TX_COUNT)return -1;m.qcount++;ack_sent++;return 0;}
'''
tail=r'''
static void init(void){m=(typeof(m)){0};m.channel=&m.ch;m.ch.up=1;m.gl.rx.data=&m;m.qr_started=true;held=tx_error=ack_sent=delivered=0;qcom_qrtr_init(&m.qr,1,send,0);m.qr.resume_count=1;m.qr.resume[0].node=9;m.qr.resume[0].port=100;m.qr.resume[0].local_port=0x4002;}
int main(void){init();m.qcount=M_TX_COUNT;held=1;m_ipc_poll();assert(!m.failed && m.qr.resume_count==1 && !ack_sent);held=0;m_ipc_poll();assert(!m.failed && !m.qr.resume_count && ack_sent==1 && m.qcount==1);m_ipc_poll();assert(!m.qcount && ack_sent==1);init();m.qr.resume_failed=1;m_ipc_poll();assert(m.failed);init();m.gl.broken=1;m_ipc_poll();assert(m.failed);init();m.qcount=1;tx_error=-1;m_ipc_poll();assert(m.failed);puts("PASS: actual worker drains TX before retrying QRTR resume and fails closed on lost acknowledgment capacity");}
'''
(out/'qrtr-worker.c').write_text(prefix+code+tail)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=gnu11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'qrtr-worker.c'),str(out/'QcomQrtr.c'),'-o',str(out/'check-qrtr-worker')],env=env,check=True)
subprocess.run([str(out/'check-qrtr-worker')],env=env,check=True)
