#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "QcomSmp2p.h"
#define MPSS_SIZE 0xe600000u
#define META_MAX 65536u
#define M_RMT_SIZE 0x280000u
#define PAGE_SIZE 16384u
static struct {bool xo_pinned,pas_owed,started,shared_owed,qmp_owed,votes_owed,vote_uncertain,stop_verified;QcomSmp2p sp;int master;uint32_t state;struct{uint32_t remote_count;}qr;uintptr_t firmware,metadata,shared,args;struct{void *image[4];uint32_t bytes[4];}fs;}m;
static unsigned rawcalls,mode,shutdowns,reclaims,poweroffs,qmpoffs,unmaps,frees,sleeps;
static int shut_result,reclaim_result;static uint64_t now;
static uint64_t m_now(void){return now;}
static void m_sleep(unsigned ms){now+=ms;sleeps++;}
static void m_smc_raw(uint64_t r[8]){
 assert(r[1]==2 && r[2]==4 && r[3]==5 && r[4]==6 && r[5]==7 && r[7]==0);
 if(mode==1){if(rawcalls==0){assert(r[0]==0x42000201);r[0]=1;r[1]=99;r[6]=0x1234;}else{assert(r[0]==1 && r[6]==0x1234);r[0]=0;r[1]=9;}}
 else if(mode==2)r[0]=(uint64_t)-12;
 else if(mode==3){r[0]=1;r[6]=123;}
 else {r[0]=0;r[1]=0;}
 rawcalls++;
}
static int m_pas(unsigned cmd,uint32_t info,uint64_t b,uint64_t c){assert(cmd==6 && info==1 && !b && !c);shutdowns++;return shut_result;}
static int m_assign(bool b){assert(!b);reclaims++;return reclaim_result;}
static int m_vote(bool b){assert(!b);poweroffs++;return 0;}
static int m_qsend(bool b){assert(!b);qmpoffs++;return 0;}
int qcom_smp2p_update(QcomSmp2p *s,int index,uint32_t mask,uint32_t value){(void)s;assert(!index && mask==1 && value==1);return 1;}
static void m_ring(unsigned client,unsigned signal){assert(client==2 && signal==2);}
static void m_smp_poll(void){m.state|=8;}
static void ml_io_unmap(uintptr_t p,size_t n){assert(p && n);unmaps++;}
static void kfree_data(void *p,size_t n){assert(p && n);frees++;}
static void sweet_log_sync(void){}
