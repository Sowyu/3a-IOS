#!/usr/bin/env python3
"""Exercise the production worker's QRTR acknowledgment retry ordering."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent;out=root/'tests/out'
s=(out/'ModemRuntime.inc').read_text();code=s[s.index('static int m_qsend_packet('):s.index('static void m_rmtfs_receive(')]+s[s.index('static void m_ipc_poll('):s.index('static bool m_shutdown_request(')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "QcomQrtr.h"
#define M_TX_COUNT 32u
#define QcomGlinkAgain 1
static struct {uintptr_t smem;struct {struct {void *data;}rx;int broken;}gl;struct {int up;} ch,*channel;QcomQrtr qr;bool qr_started,failed;uint32_t rxoff,qhead,qcount;struct {uint32_t len;uint8_t data[512];}queue[M_TX_COUNT];}m;
static int held,tx_error;static unsigned delivered,received;
static int m_item(unsigned id,unsigned min,uint32_t *off,uint32_t *bytes){(void)id;(void)min;(void)off;(void)bytes;return -1;}
#define qcom_glink_set_rx(g,d,n) ((void)(g),(void)(d),(void)(n))
#define qcom_glink_poll(g) ((void)(g))
static int mock_send(const void *p,unsigned len){(void)p;(void)len;delivered++;return tx_error?tx_error:held?QcomGlinkAgain:0;}
#define qcom_glink_send(g,c,d,n) ((void)(g),(void)(c),mock_send(d,n))
static uint32_t le32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

'''
tail=r'''
static void receive(void *ctx,QcomQrtr *q,uint32_t port,uint32_t node,uint32_t from,const uint8_t *data,uint32_t len){(void)ctx;(void)q;(void)port;(void)node;(void)from;(void)data;(void)len;received++;}

static void init(void){m=(typeof(m)){0};m.channel=&m.ch;m.ch.up=1;m.gl.rx.data=&m;m.qr_started=true;held=tx_error=delivered=received=0;qcom_qrtr_init(&m.qr,1,m_qsend_packet,0);m.qr.resume_count=1;m.qr.resume[0].node=9;m.qr.resume[0].port=100;m.qr.resume[0].local_port=0x4002;}
int main(void){init();m.qcount=M_TX_COUNT;held=1;m_ipc_poll();assert(!m.failed && m.qr.resume_count==1 && !m.qr.resumes_sent);held=0;m_ipc_poll();assert(!m.failed && !m.qr.resume_count && m.qr.resumes_sent==1 && m.qcount==1);m_ipc_poll();assert(!m.qcount && m.qr.resumes_sent==1);init();m.qr.resume_failed=1;m_ipc_poll();assert(m.failed);init();m.gl.broken=1;m_ipc_poll();assert(m.failed);init();m.qcount=1;tx_error=-1;m_ipc_poll();assert(m.failed);init();m.qcount=M_TX_COUNT;uint8_t ordinary[52]={1,0,0,0,QCOM_QRTR_DATA};assert(m_qsend_packet(0,ordinary,sizeof ordinary)==-1 && m.failed);
init();assert(m_qsend_packet(0,ordinary,513)==-1 && m.failed);
init();assert(!qcom_qrtr_add_service(&m.qr,0,0,0x4002,receive,0));m.qr.resume_count=0;m.qcount=M_TX_COUNT;uint8_t packet[32]={1,0,0,0,QCOM_QRTR_DATA};packet[8]=9;packet[12]=100;packet[16]=1;packet[24]=1;packet[28]=2;packet[29]=0x40;
assert(!qcom_qrtr_receive(&m.qr,packet,sizeof packet) && received==1 && !m.failed && m.qr.resume_count==1);
held=1;for(unsigned i=0;i<5;i++){m_ipc_poll();assert(!m.failed && received==1 && m.qr.resume_count==1 && !m.qr.resumes_sent);}
held=0;m_ipc_poll();assert(!m.failed && received==1 && !m.qr.resume_count && m.qr.resumes_sent==1 && m.qcount==1);
m_ipc_poll();assert(!m.failed && received==1 && m.qr.resumes_sent==1 && !m.qcount && m.qr_started);
puts("PASS: actual worker and queue callback retain full-queue resumes while ordinary reply loss fails; acknowledgment overflow also fails closed on lost acknowledgment capacity");}
'''
(out/'qrtr-worker.c').write_text(prefix+code+tail)
env=dict(os.environ,TMPDIR=str(out/'tmp'))
subprocess.run(['cc','-save-temps=obj','-std=gnu11','-Wall','-Wextra','-Werror','-DMODEM_PROTOCOL_HOST_TEST','-fsanitize=address,undefined','-I'+str(out),str(out/'qrtr-worker.c'),str(out/'QcomQrtr.c'),'-o',str(out/'check-qrtr-worker')],env=env,check=True)
subprocess.run([str(out/'check-qrtr-worker')],env=env,check=True)
