#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
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
typedef uintptr_t vm_offset_t;typedef uint64_t vm_size_t;
/* 16 KiB pages, as XNU's io_map: it maps trunc_page(pa) for round_page(size+offset)
 * and returns the offset address. pmap_remove panics on an unaligned range. */
#define PAGE_MASK 0x3fffu
#define round_page(x) (((uintptr_t)(x)+PAGE_MASK)&~(uintptr_t)PAGE_MASK)
static struct {uintptr_t base,size;bool live,contig;} maps[16];static unsigned nmaps,kfrees,wrong_unmaps;
static uintptr_t fake_map(uint64_t pa,uint64_t size){uintptr_t off=pa&PAGE_MASK,base=0x10000000u*(nmaps+1);
 maps[nmaps++]=(typeof(maps[0])){base,round_page(size+off),true,false};return base+off;}
/* 2290: PAS metadata is contiguous kernel memory, released with kmem_free. */
static uintptr_t fake_contig(uint64_t size){uintptr_t base=0x10000000u*(nmaps+1);maps[nmaps++]=(typeof(maps[0])){base,size,true,true};return base;}
static void ml_io_unmap(uintptr_t p,uint64_t bytes){
 if(!p || !bytes || (p&PAGE_MASK) || (bytes&PAGE_MASK)){fprintf(stderr,"UNALIGNED unmap p=%#lx bytes=%#llx\n",(unsigned long)p,(unsigned long long)bytes);abort();}
 for(unsigned i=0;i<nmaps;i++)if(maps[i].base==p){assert(maps[i].live && maps[i].size==bytes);maps[i].live=false;if(maps[i].contig)wrong_unmaps++;else unmaps++;return;}
 assert(!"unmap of an address io_map never returned");}
static int kernel_map_object;
#define kernel_map (&kernel_map_object)
static void kmem_free(int *map,uintptr_t p,uint64_t bytes){assert(map==kernel_map);
 for(unsigned i=0;i<nmaps;i++)if(maps[i].base==p){assert(maps[i].contig && maps[i].live && maps[i].size==bytes);maps[i].live=false;kfrees++;return;}
 assert(!"kmem_free of memory never allocated");}
static unsigned live_maps(void){unsigned n=0;for(unsigned i=0;i<nmaps;i++)n+=maps[i].live;return n;}
static void kfree_data(void *p,unsigned bytes){assert(p && bytes);frees++;}
static void sweet_log_sync(void){}

static int m_vote(bool on){assert(!on);votes++;return vfail;}

static void battery_snapshot_clear(void){}
