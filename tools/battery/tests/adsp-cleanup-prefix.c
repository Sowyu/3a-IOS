#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
struct adsp_image {unsigned pas;bool owed,started;uintptr_t mapping,metadata;uint64_t bytes;void *mdt;unsigned mdt_bytes;};
static struct {struct adsp_image images[2];struct {void *out;} sp;int master;unsigned state;bool vote_uncertain,qmp_owed,votes_owed,proxy_released,xo_pinned;uintptr_t db,rsc,qmpmem,ipcc,mutex,smem;} m;
#define M_SM_SIZE 0x200000
#define META_MAX 65536
static unsigned calls[100],count,unmaps,frees,fail_pas,qfail,bits,clock_ms,retries,no_ack,vfail,votes;
static uint64_t m_now(void){return clock_ms;}
static void m_sleep(unsigned ms){clock_ms+=ms;}
static int m_pas(unsigned pas,unsigned cmd,unsigned info,unsigned b,unsigned c){assert(cmd==6 && info==1 && b==0 && c==0);calls[count++]=pas;if(pas==fail_pas)return -EIO;if(retries){retries--;return -EINVAL;}return 0;}
static void qcom_smp2p_update(void *p,int index,unsigned mask,unsigned value){(void)p;assert(index==0 && mask==1);bits=value;}
static void m_ring(unsigned client,unsigned signal){assert(client==3 && signal==2);}
static void m_smp_poll(void){if(!no_ack)m.state|=8;}
static int m_qsend(bool on){assert(!on);return qfail;}
static void ml_io_unmap(uintptr_t p,uint64_t bytes){assert(p && bytes);unmaps++;}
static void kfree_data(void *p,unsigned bytes){assert(p && bytes);frees++;}
static void sweet_log_sync(void){}

static int m_vote(bool on){assert(!on);votes++;return vfail;}

static void battery_snapshot_clear(void){}
