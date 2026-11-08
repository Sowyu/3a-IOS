/* Host model for sweet_wpss.c and wifi/WpssRuntime.inc (2240 to 2242). The actual sources are compiled unchanged against
 * this: fake SMEM, TCSR mutex, IPCC, RSC, cmd-db, AOSS QMP, TrustZone (SMC) and a WPSS that answers over SMP2P. Firmware comes
 * from a read-only modem_a image file given on the command line. Nothing here touches hardware. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
typedef uintptr_t vm_offset_t;typedef size_t vm_size_t;typedef uint32_t ppnum_t;typedef uint64_t addr64_t;
typedef int kern_return_t;typedef void *pmap_t;typedef void *vm_map_t;typedef int thread_t;typedef int wait_result_t;
#define KERN_SUCCESS 0
#define THREAD_NULL 0
#define PAGE_SIZE 16384u
#define PAGE_MASK (PAGE_SIZE-1)
#define round_page(x) (((uint64_t)(x)+PAGE_MASK)&~(uint64_t)PAGE_MASK)
#define ptoa_64(p) ((uint64_t)(p)<<14)
#define VM_WIMG_IO 7u
#define VM_WIMG_WCOMB 4u
#define KMA_DATA 1
#define KMA_ZERO 2
#define VM_KERN_MEMORY_IOKIT 3
#define Z_WAITOK 1
#define Z_ZERO 2
#define kernel_map ((vm_map_t)0)
#define kernel_pmap ((pmap_t)0)
#define W_BARRIER() do{}while(0)

/* The log: every line is kept so tests can look for it; WPSS_VERBOSE=1 also prints it. */
static char hlog[1<<20];static size_t hlog_len;
static int hprintf(const char *fmt,...){
 char line[1024];va_list ap;va_start(ap,fmt);int n=vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);
 size_t k=strlen(line);if(hlog_len+k<sizeof(hlog)){memcpy(hlog+hlog_len,line,k);hlog_len+=k;hlog[hlog_len]=0;}
 if(getenv("WPSS_VERBOSE"))fputs(line,stdout);return n;
}
#define printf hprintf
static bool logged(const char *s){return strstr(hlog,s)!=NULL;}

/* Time in microseconds. Every wait advances it and lets the fake WPSS act. */
static uint64_t now_us;
static void remote_tick(void);
static uint64_t w_now(void){return now_us/1000;}
static void w_sleep(unsigned ms){now_us+=(uint64_t)ms*1000;remote_tick();}
static void delay(unsigned us){now_us+=us;}

/* Fake memory regions, each mapped as io_map does: trunc_page(pa) for round_page(size+offset), offset address returned. */
struct region {uint64_t pa,size;unsigned flags;uint8_t *mem __attribute__((unused));vm_offset_t va;uint64_t mapped;int maps,unmaps,writes;};
#define SMEM_PA 0x81d00000u
enum {R_SMEM,R_MUTEX,R_IPCC,R_QMP,R_RSC,R_DB,R_CARVE,R_SPMI_CORE,R_SPMI_CNFG,R_SPMI_OBS,R_SPMI_CHNL,R_GIC,R_SMMU,R_COUNT};
/* SPMI arbiter v7 as on this phone: PMK8550 GPIO5 (PPID 0x0bc) is APID 37 owned by EE 0. Tests may add a decoy APID 12 owned by EE 3. */
#define GPIO5_APID 37u
static struct region regions[R_COUNT]={
 {0x81d00000u,0x200000u,VM_WIMG_IO},{0x1f43000u,0x1000u,VM_WIMG_IO},{0x405000u,0x1000u,VM_WIMG_IO},{0xc300000u,0x400u,VM_WIMG_IO},
 {0x17a20000u,0x4000u,VM_WIMG_IO},{0x81c60000u,0x20000u,VM_WIMG_IO},{0x9bb00000u,0x1900000u,VM_WIMG_WCOMB},
 {0x0c400000u,0x3000u,VM_WIMG_IO},{0x0c42d000u,0x4000u,VM_WIMG_IO},{0x0c440000u+0x20u*GPIO5_APID,0x20u,VM_WIMG_IO},{0x0c500000u+0x1000u*GPIO5_APID,0x20u,VM_WIMG_IO},
 {0x17100000u,0x1000u,VM_WIMG_IO},
 /* 2252 (W8 survey): apps_smmu, the whole window IPA maps too. */
 {0x15000000u,0x100000u,VM_WIMG_IO}};
/* 2244: the WPSS watchdog probe must only read the GIC. w_wr counts every write, so a control can prove none happened. */
static unsigned gic_writes;
static int fail_map=-1;
static void regions_init(void){
 for(unsigned i=0;i<R_COUNT;i++){
  struct region *r=&regions[i];uint64_t off=r->pa&PAGE_MASK;r->mapped=round_page(r->size+off);
  if(!r->mem){void *p=NULL;assert(!posix_memalign(&p,PAGE_SIZE,r->mapped));r->mem=p;}
  memset(r->mem,0,r->mapped);r->va=(vm_offset_t)r->mem+off;r->maps=r->unmaps=r->writes=0;
 }
}
static uint8_t *at(int i,unsigned off){return (uint8_t *)regions[i].va+off;}
static vm_offset_t ml_io_map_unmappable(vm_offset_t pa,vm_size_t size,unsigned flags){
 for(int i=0;i<R_COUNT;i++)if(regions[i].pa==pa){
  assert(size==regions[i].size && flags==regions[i].flags);
  if(i==fail_map)return 0;
  regions[i].maps++;return regions[i].va;
 }
 fprintf(stderr,"FAIL: unexpected mapping pa=%#lx size=%#zx\n",(unsigned long)pa,size);abort();
}
static void ml_io_unmap(vm_offset_t va,vm_size_t size){
 for(int i=0;i<R_COUNT;i++)if(va>=(vm_offset_t)regions[i].mem && va<(vm_offset_t)regions[i].mem+regions[i].mapped){
  if(va!=(vm_offset_t)regions[i].mem || size!=regions[i].mapped){fprintf(stderr,"FAIL: UNALIGNED unmap region=%d va_off=%#lx bytes=%#zx\n",i,(unsigned long)(va-(vm_offset_t)regions[i].mem),size);abort();}
  regions[i].unmaps++;assert(regions[i].unmaps<=regions[i].maps);return;
 }
 fprintf(stderr,"FAIL: unmap of an unknown address\n");abort();
}

/* PAS metadata and, from 2253 (W9), the ring page and the page table: contiguous memory with a fake physical address.
 * One slot per live allocation, so a run can hold the metadata, a ring page and a table at once, each at its own PA and
 * below the 36-bit limit w_contig and w9_alloc check. Slot 0 is the metadata in every run before W9, so the earlier
 * stages' tests still read it as meta_buf. */
#define META_PA 0x8c3400000ull  /* below the 36-bit limit w_contig checks */
#define CONTIG_SLOTS 4
#define CONTIG_BYTES 65536u
static uint8_t contig_mem[CONTIG_SLOTS][CONTIG_BYTES] __attribute__((aligned(16384)));
#define meta_buf (contig_mem[0])
static struct {uint64_t pa;size_t bytes;int used;} contig[CONTIG_SLOTS];
static int meta_allocs,meta_frees,syncs;
static int contig_find(vm_offset_t va){
 for(int i=0;i<CONTIG_SLOTS;i++)if(contig[i].used && va>=(vm_offset_t)contig_mem[i] && va<(vm_offset_t)contig_mem[i]+contig[i].bytes)return i;
 return -1;
}
static kern_return_t kmem_alloc_contig(vm_map_t m,vm_offset_t *va,vm_size_t size,vm_offset_t mask,int a,int b,int fl,int tag){
 (void)m;(void)mask;(void)a;(void)b;(void)fl;(void)tag;
 assert(size && size<=CONTIG_BYTES);
 for(int i=0;i<CONTIG_SLOTS;i++)if(!contig[i].used){
  contig[i].used=1;contig[i].bytes=size;contig[i].pa=META_PA+0x4000000ull*(uint64_t)i;
  memset(contig_mem[i],0,size);meta_allocs++;*va=(vm_offset_t)contig_mem[i];return KERN_SUCCESS;
 }
 fprintf(stderr,"FAIL: no contiguous slot left\n");abort();
}
static void kmem_free(vm_map_t m,vm_offset_t va,vm_size_t size){
 (void)m;int i=contig_find(va);assert(i>=0 && va==(vm_offset_t)contig_mem[i] && size==contig[i].bytes);
 contig[i].used=0;meta_frees++;
}
static ppnum_t pmap_find_phys(pmap_t p,addr64_t va){
 (void)p;int i=contig_find((vm_offset_t)va);
 assert(i>=0);return (ppnum_t)((contig[i].pa+(va-(addr64_t)contig_mem[i]))>>14);
}
static void sweet_dma_sync_va(const void *va,size_t len){
 int i=contig_find((vm_offset_t)va);assert(i>=0 && len<=contig[i].bytes);syncs++;
}
static int kallocs;
static void *kalloc_data(size_t n,int f){(void)f;kallocs++;return calloc(1,n);}
static void kfree_data(void *p,size_t n){(void)n;kallocs--;free(p);}

/* Shared locks: balanced, never nested. */
static int lock_rsc,lock_scm,lock_qmp,lock_smem;
#define LOCKFN(name,var) static void name##_lock(void){assert(!var);var=1;} static void name##_unlock(void){assert(var);var=0;}
LOCKFN(sweet_apps_rsc,lock_rsc) LOCKFN(sweet_scm,lock_scm) LOCKFN(sweet_qmp,lock_qmp) LOCKFN(sweet_smem,lock_smem)
static void sweet_log_sync(void){}

/* modem_a, read only, from a file. Any other partition fails the test. */
static FILE *modem_a;static uint64_t modem_a_bytes;static int fail_fw;
static int sweet_ufs_part_size(const char *n,uint8_t *g,uint64_t *bytes){(void)g;assert(!strcmp(n,"modem_a"));*bytes=modem_a_bytes;return 0;}
static int sweet_ufs_read_part(const char *n,uint64_t off,void *dst,uint64_t len){
 assert(!strcmp(n,"modem_a"));if(fail_fw)return EIO;
 if(fseek(modem_a,(long)off,SEEK_SET) || fread(dst,1,(size_t)len,modem_a)!=len)return EIO;return 0;}

/* The v7 observer channel: a one-byte EXT_READL completes at once with the register's value, unless told to fail or hang. */
static uint8_t gpio5_regs[256];static int spmi_fail,spmi_hang;static unsigned spmi_cmds;
static void spmi_command(uint32_t cmd){
 spmi_cmds++;uint32_t st=1,data=0;unsigned reg=(cmd>>4)&0xff;
 if(cmd!=((1u<<27)|(reg<<4)))st=1|8;  /* not a one-byte read: dropped */
 else if(spmi_fail)st=1|2;else data=gpio5_regs[reg];
 if(spmi_hang)st=0;
 memcpy((void *)(regions[R_SPMI_OBS].va+8),&st,4);memcpy((void *)(regions[R_SPMI_OBS].va+0x18),&data,4);
}
/* The EE 0 write channel: a one-byte EXT_WRITEL of WDATA0 into the pin's register. Every write is recorded. */
struct pmicw {unsigned reg;uint8_t value;};static struct pmicw pmic_writes[32];static unsigned pmic_nwrites;
static int chan_fail_at=-1,stuck_reg=-1;
static void spmi_channel(uint32_t cmd){
 uint32_t data;memcpy(&data,(void *)(regions[R_SPMI_CHNL].va+0x10),4);
 unsigned reg=(cmd>>4)&0xff;uint32_t st=1;
 if(cmd!=(reg<<4) || data>0xff){st=1|8;}           /* only a one-byte EXT_WRITEL (opcode 0, count 0) is taken */
 else if((int)pmic_nwrites==chan_fail_at){st=1|2;}
 else {if((int)reg!=stuck_reg)gpio5_regs[reg]=(uint8_t)data;}
 if(pmic_nwrites<32)pmic_writes[pmic_nwrites++]=(struct pmicw){reg,(uint8_t)data};
 memcpy((void *)(regions[R_SPMI_CHNL].va+8),&st,4);
}
/* 2252 (W8 survey): apps_smmu as this phone presents it (ast159/ast170/ast260): 114 SMRs, 72 banks at 0x80000, sCR0 with
 * USFCFG set, SMRs 0 to 4 as ABL leaves them. Every read is checked against the set our code has already made on the phone,
 * every write aborts, and every read must happen under the apps_smmu claim. Banks a WLAN SMR selects may also be read at
 * TCR2/TCR/MAIR0/MAIR1, the offsets IPA reads on the banks it leases. */
#define SMMU_NSMR 114u
#define SMMU_NCB 72u
static int smmu_claimed,smmu_claim_other;static unsigned smmu_table_reads;static int opt_usf_in_mission;
/* 2253 (W9): a fault recorded in the leased bank, to exercise the teardown's write-one-to-clear path. It is raised the
 * first time the teardown reads FSR after the bank has been disabled. */
static int smmu_fault_at_teardown,smmu_fault_raised;
static int sweet_apps_smmu_claim(void){if(smmu_claim_other)return 0;assert(!smmu_claimed);smmu_claimed=1;return 1;}
static void sweet_apps_smmu_release(void){assert(smmu_claimed);smmu_claimed=0;}
static bool smmu_hits(uint32_t smr,unsigned sid){uint32_t m=(smr>>16)&0x7fffu;return (smr>>31) && !((smr^sid)&0x7fffu&~m);}
static bool smmu_wlan_bank(unsigned c){
 for(unsigned i=0;i<SMMU_NSMR;i++){
  uint32_t smr,s2;memcpy(&smr,(void *)(regions[R_SMMU].va+0x800+4*i),4);memcpy(&s2,(void *)(regions[R_SMMU].va+0xc00+4*i),4);
  if((smmu_hits(smr,0x1400) || smmu_hits(smr,0x1401)) && !((s2>>16)&3u) && (s2&255u)==c)return true;
 }
 return false;
}
enum {W9S_IDLE=0,W9S_CONFIG,W9S_ON,W9S_INSTALLED,W9S_RELEASING,W9S_DONE};
static int smmu_w9_state=W9S_IDLE,smmu_w9_bank=-1,smmu_w9_slot=-1,smmu_w9_vmid=-1;
static unsigned smmu_write_bad,smmu_writes,w9_installs,w9_releases;
static bool w9_seen_cbar,w9_seen_on,w9_seen_ttbr,w9_seen_fault,w9_seen_off;
static uint32_t smmu_w9_smr_old,smmu_w9_s2cr_old,smmu_w9_cbar_old,smmu_w9_cba2r_old;
static uint64_t smmu_w9_ttbr;
static void smmu_read_check(unsigned o){
 if(!smmu_claimed){fprintf(stderr,"FAIL: SMMU read without the claim off=%#x\n",o);abort();}
 bool ok=o==0 || o==0x20 || o==0x24 || o==0x28 || o==0x48;
 if(o>=0x800 && o<0x800+4*SMMU_NSMR && !(o&3))ok=true;
 if(o>=0xc00 && o<0xc00+4*SMMU_NSMR && !(o&3))ok=true;
 if(o>=0x1000 && o<0x1000+4*SMMU_NCB && !(o&3))ok=true;
 if(o>=0x1800 && o<0x1800+4*SMMU_NCB && !(o&3))ok=true;
 if(o>=0x800 && o<0x2000)smmu_table_reads++;
 if(o>=0x80000 && o<0x80000+0x1000*SMMU_NCB){
  unsigned c=(o-0x80000)/0x1000,r=(o-0x80000)%0x1000;smmu_table_reads++;
  if(r==0 || r==0x20 || r==0x24 || r==0x28 || r==0x2c || r==0x58)ok=true;
  /* TCR, TCR2, MAIR0 and MAIR1: a bank a WLAN SMR selects (the W8 rule), or the bank this run's lease has taken, which
   * is read back while it is still disabled and not yet routed. */
  if((r==0x10 || r==0x30 || r==0x38 || r==0x3c) && (smmu_wlan_bank(c) || (int)c==smmu_w9_bank))ok=true;
  if(r==0x58 && (int)c==smmu_w9_bank)ok=true;
 }
 if(!ok){fprintf(stderr,"FAIL: SMMU read outside the proven set off=%#x\n",o);abort();}
}
/* A physical address that one of the run's live contiguous allocations covers. The write model uses it to prove a TTBR
 * points at a table this run built, not at some address the code invented. */
static bool contig_pa_live(uint64_t pa){
 for(int i=0;i<CONTIG_SLOTS;i++)if(contig[i].used && pa>=contig[i].pa && pa<contig[i].pa+contig[i].bytes)return true;
 return false;
}
/* The live allocation whose low 32 address bits a split TTBR0 write carries, or 0. The full PA is what the high word then
 * has to agree with, so both halves are checked against one allocation rather than against themselves. */
static uint64_t contig_pa_from_low(uint32_t low){
 for(int i=0;i<CONTIG_SLOTS;i++)if(contig[i].used && low==(uint32_t)contig[i].pa)return contig[i].pa;
 return 0;
}
/* ---- 2253 (W9): the write model -------------------------------------------------------------------------------------
 * W8's model aborted on any SMMU write, because W8 wrote none. W9 is the first stage that writes, so the model judges
 * writes instead of forbidding them, and it deliberately does NOT mirror the code's instruction order: a judge that
 * repeats the order it is judging passes a shared misreading. It enforces the three things that make the lease safe:
 *
 *  1. Geometry and phase. A write is refused outright unless it lands on an offset our own code has already read on this
 *     phone, and unless the lease is in a phase that allows it: a bank may be configured only while it is disabled, its
 *     table must be in place before it is enabled, nothing may be routed to it before it is enabled, and the SMR must be
 *     published last, so no stream can ever reach a half-written table. The release's route-to-fault must come first, and
 *     the words the lease found must be the words it restores.
 *  2. Arithmetic. CBAR must carry a nonzero VMID in IPA's lease form (0x1f300 | vmid); the TTBR must be page-aligned,
 *     carry this bank's ASID in its top byte, and point at the table this run allocated; the SMR must be valid, carry mask
 *     1 and the device's stream id 0x1400; the S2CR must name this bank and translate.
 *  3. The claim. Every write happens under sweet_apps_smmu_claim, and the model fails a write without it.
 *
 * A hard failure prints and sets smmu_write_bad; the tests assert it is 0 and that w9_installs and w9_releases are each 1. */
static void smmu_write_check(unsigned o,uint32_t v){
 smmu_writes++;
 bool ok=false;
 if(!smmu_claimed){fprintf(stderr,"FAIL: SMMU write without the claim off=%#x\n",o);abort();}
 if(!(o==0x48 || (o>=0x800 && o<0x800+4*SMMU_NSMR && !(o&3)) || (o>=0xc00 && o<0xc00+4*SMMU_NSMR && !(o&3)) ||
      (o>=0x1000 && o<0x1000+4*SMMU_NCB && !(o&3)) || (o>=0x1800 && o<0x1800+4*SMMU_NCB && !(o&3)) ||
      (o>=0x80000 && o<0x80000+0x1000*SMMU_NCB))){
  fprintf(stderr,"FAIL: SMMU write outside the proven set off=%#x\n",o);abort();
 }
 const int c=o>=0x80000?(int)((o-0x80000)/0x1000):-1,r=o>=0x80000?(int)((o-0x80000)%0x1000):-1;
 const int slot=o>=0x800&&o<0xc00?(int)((o-0x800)/4):(o>=0xc00&&o<0x1000?(int)((o-0xc00)/4):-1);
 const int bi=o>=0x1000&&o<0x1400?(int)((o-0x1000)/4):(o>=0x1800&&o<0x1c00?(int)((o-0x1800)/4):-1);
 switch(smmu_w9_state){
 case W9S_IDLE:
  /* The first write of a lease disables a free bank: SCTLR = 0, on a bank nothing references. */
  if(c>=0 && r==0 && v==0 && !smmu_wlan_bank((unsigned)c)){smmu_w9_bank=c;smmu_w9_state=W9S_CONFIG;ok=true;}
  break;
 case W9S_CONFIG:
  if(c==smmu_w9_bank && r==0 && v==0)ok=true;                                     /* a repeat disable is harmless */
  else if(c==smmu_w9_bank && r==0x20 && (v&4095u)==0){
   /* TTBR0, low word. The write is split in two, so this half is the PA's low 32 bits and the next one carries the rest
    * plus this bank's ASID; together they must name a PA inside one of the run's live contiguous allocations, which is the
    * table the code built. A TTBR pointing anywhere else is not this stage's table. */
   uint64_t pa=contig_pa_from_low(v);
   if(pa){w9_seen_ttbr=true;
    /* The word the high half must carry: the PA's bits 32 and up, plus this bank's ASID in the top byte. */
    smmu_w9_ttbr=(pa>>32)|((uint64_t)(unsigned)(smmu_w9_bank+1)<<16);ok=true;}
  } else if(c==smmu_w9_bank && r==0x24 && w9_seen_ttbr && v==(uint32_t)smmu_w9_ttbr)ok=true;
  else if(c==smmu_w9_bank && (r==0x28 || r==0x2c || r==0x3c || r==0x58) && v==0)ok=true;
  else if(c==smmu_w9_bank && r==0x618 && v==0)ok=true;                                /* a VMID invalidate */
  else if(c==smmu_w9_bank && (r==0x10 || r==0x30 || r==0x38))ok=true;               /* TCR2, TCR, MAIR0 */
  else if(bi==smmu_w9_bank && o==0x1000+4*(unsigned)smmu_w9_bank && (v&0xfff00u)==0x1f300u && (v&255u)){
   smmu_w9_vmid=(int)(v&255u);w9_seen_cbar=true;ok=true;
  } else if(bi==smmu_w9_bank && o==0x1800+4*(unsigned)smmu_w9_bank && v==1)ok=true;
  else if(c==smmu_w9_bank && r==0 && v==0xc00027u && w9_seen_cbar && w9_seen_ttbr){
   w9_seen_on=true;smmu_w9_state=W9S_ON;ok=true;                                    /* enable, after the config */
  }
  break;
 case W9S_ON:
  /* Enabled and still unrouted: the invalidate, then the S2CR, then the SMR that publishes the route. */
  if(c==smmu_w9_bank && r==0x618 && v==0)ok=true;
  else if(slot>=0 && smmu_w9_slot<0 && o==0xc00+4*(unsigned)slot && (v&255u)==(unsigned)smmu_w9_bank &&
          ((v>>16)&3u)==0){smmu_w9_slot=slot;ok=true;}
  else if(slot>=0 && slot==smmu_w9_slot && o==0x800+4*(unsigned)slot && (v>>31) && ((v>>16)&0x7fffu)==1u &&
          (v&0x7fffu)==0x1400u){
   smmu_w9_state=W9S_INSTALLED;w9_installs++;ok=true;
  }
  break;
 case W9S_INSTALLED:
  /* The one legal write with the lease live is the release's first: route the stream to a fault. */
  if(slot==smmu_w9_slot && o==0xc00+4*(unsigned)smmu_w9_slot && v==(2u<<16)){
   w9_seen_fault=true;smmu_w9_state=W9S_RELEASING;ok=true;
  }
  break;
 case W9S_RELEASING:
  if(c==smmu_w9_bank && r==0 && v==0){w9_seen_off=true;ok=true;}
  else if(c==smmu_w9_bank && r==0x618 && v==0 && w9_seen_off)ok=true;
  else if(c==smmu_w9_bank && (r==0x20 || r==0x24 || r==0x28 || r==0x2c) && v==0 && w9_seen_off)ok=true;
  else if(c==smmu_w9_bank && r==0x58 && (v&~0x600u) && w9_seen_off)ok=true;
  else if(slot==smmu_w9_slot && o==0x800+4*(unsigned)smmu_w9_slot && v==smmu_w9_smr_old)ok=true;
  else if(slot==smmu_w9_slot && o==0xc00+4*(unsigned)smmu_w9_slot && v==smmu_w9_s2cr_old)ok=true;
  else if(bi==smmu_w9_bank && o==0x1000+4*(unsigned)smmu_w9_bank && v==smmu_w9_cbar_old)ok=true;
  else if(bi==smmu_w9_bank && o==0x1800+4*(unsigned)smmu_w9_bank && v==smmu_w9_cba2r_old){
   smmu_w9_state=W9S_DONE;w9_releases++;ok=true;
  }
  break;
 case W9S_DONE:
  break;
 }
 if(!ok){smmu_write_bad++;fprintf(stderr,"FAIL: SMMU write outside the W9 lease phase %d off=%#x value=%#x\n",smmu_w9_state,o,v);}
}
/* The IPA driver's access names, which W9 uses so every SMMU access is an explicit ldr/str on the phone. On the host they
 * are the same checked window as w_rd/w_wr, so a write still goes through the lease model. */
static uint32_t w_rd(vm_offset_t p,unsigned o);
static void w_wr(vm_offset_t p,unsigned o,uint32_t v);
static uint32_t m_rd(vm_offset_t p,unsigned o){return w_rd(p,o);}
static void m_wr(vm_offset_t p,unsigned o,uint32_t v){w_wr(p,o,v);}
/* Register access with the side effects the runtime depends on. */
struct rscw {unsigned off;uint32_t value;};static struct rscw rsc_writes[256];static unsigned rsc_nwrites;
static uint32_t ipcc_rings[64];static unsigned ipcc_nrings;
static uint32_t w_rd(vm_offset_t p,unsigned o){
 if(p==regions[R_SMMU].va){
  smmu_read_check(o);
  /* 2253: a fault raised in the leased bank for the teardown's own FSR read, so the write-one-to-clear path is tested. */
  if(smmu_fault_at_teardown && !smmu_fault_raised && smmu_w9_state==W9S_RELEASING && p==regions[R_SMMU].va &&
     smmu_w9_bank>=0 && o==0x80000+0x1000u*(unsigned)smmu_w9_bank+0x58){
   uint32_t f=0x40a;memcpy((void *)(p+o),&f,4);smmu_fault_raised=1;
  }
 }
 uint32_t v;memcpy(&v,(void *)(p+o),4);return v;
}
static void w_wr(vm_offset_t p,unsigned o,uint32_t v){
 if(p==regions[R_SMMU].va){
  smmu_write_check(o,v);if(smmu_write_bad)return;
  /* 2253: FSR is write-one-to-clear on the real SMMU (ARM IHI0062D.c), so the model must be that or the teardown's fault
   * clear would pass here and never work on the phone. Every other word is a plain store. */
  if(o>=0x80000 && (o-0x80000)%0x1000u==0x58u){
   uint32_t old;memcpy(&old,(void *)(p+o),4);uint32_t now=old&~v;memcpy((void *)(p+o),&now,4);return;
  }
  memcpy((void *)(p+o),&v,4);return;
 }
 memcpy((void *)(p+o),&v,4);
 for(int i=0;i<R_COUNT;i++)if(p==regions[i].va)regions[i].writes++;
 if(p==regions[R_GIC].va)gic_writes++;
 if(p==regions[R_SPMI_OBS].va && o==0)spmi_command(v);
 if(p==regions[R_SPMI_CHNL].va && o==0)spmi_channel(v);
 if(p==regions[R_SPMI_CHNL].va && o!=0 && o!=0x10){fprintf(stderr,"FAIL: channel register %#x written\n",o);abort();}
 if(p==regions[R_RSC].va){
  if(rsc_nwrites<256)rsc_writes[rsc_nwrites++]=(struct rscw){o,v};
  /* A trigger completes every command of that TCS. */
  if((o-0xd00)%0x2a0==0x24 && (v&(1u<<24)))for(unsigned i=0;i<3;i++){uint32_t done=0x10100;memcpy((void *)(p+o-0x24+0x40+i*24),&done,4);}
 }
 if(p==regions[R_IPCC].va && o==0x0c){if(ipcc_nrings<64)ipcc_rings[ipcc_nrings++]=v;remote_tick();}
}

/* QMP: a stand-in for the AOSS mailbox, which QcomQmp.c's own tests cover. */
#include "../modem/QcomQmp.h"
static char qmp_sent[8][128];static unsigned qmp_nsent;static int qmp_fail_open,qmp_fail_off;
void qcom_qmp_init(QcomQmp *q,volatile uint32_t *ram,uint32_t bytes,void (*kick)(void *),void *ctx){
 assert(lock_qmp && ram==(volatile uint32_t *)regions[R_QMP].va && bytes==0x400);memset(q,0,sizeof(*q));q->kick=kick;q->context=ctx;q->state=QcomQmpLinkWait;}
int qcom_qmp_step(QcomQmp *q){assert(lock_qmp);if(qmp_fail_open)return q->state;if(q->state!=QcomQmpOpen)q->state=QcomQmpOpen;return q->state;}
int qcom_qmp_send(QcomQmp *q,const char *text){
 assert(lock_qmp && q->state==QcomQmpOpen && strlen(text)<QCOM_QMP_MESSAGE);
 if(qmp_fail_off && strstr(text,"val: off"))return QcomQmpBusy;
 assert(qmp_nsent<8);snprintf(qmp_sent[qmp_nsent++],128,"%s",text);q->state=QcomQmpSending;return QcomQmpSending;}

/* TrustZone. Every PAS call is recorded with the time and whether the WPSS had acknowledged a stop. */
struct pas_call {unsigned cmd,pas;uint32_t info;uint64_t a,b;uint64_t t;bool ack_before;};
static struct pas_call pas_calls[128];static unsigned pas_ncalls;
static int pas_status[8];          /* status returned by command n, every time */
static int shutdown_einval;        /* PAS shutdown answers a transport failure this many times */
static int auth_interrupt;         /* auth_and_reset is interrupted once, then resumed */
static bool remote_running,remote_acked;
static void remote_start(void);
static void w_smc_raw(uint64_t r[8]){
 assert(lock_scm);
 if(r[0]==1){ /* resume of an interrupted call */
  assert(r[6]==0x77 && pas_ncalls && pas_calls[pas_ncalls-1].cmd==5);r[0]=0;r[1]=0;remote_start();return;}
 unsigned cmd=r[0]&0xff;assert(r[0]==((1u<<30)|(2u<<24)|(2u<<8)|cmd));
 assert(pas_ncalls<128);pas_calls[pas_ncalls++]=(struct pas_call){cmd,(unsigned)r[2],(uint32_t)r[1],r[3],r[4],now_us,remote_acked};
 if(cmd==5 && auth_interrupt){auth_interrupt=0;r[0]=1;r[6]=0x77;return;}
 if(cmd==6 && shutdown_einval){shutdown_einval--;r[0]=(uint64_t)(int64_t)-3145800;return;}
 r[0]=(uint64_t)(int64_t)pas_status[cmd&7];r[1]=0;
 if(cmd==5 && !pas_status[5])remote_start();
 if(cmd==6 && !pas_status[6])remote_running=false;
}

/* The fake WPSS: on auth it makes SMEM item 617 with "slave-kernel", says ready then handover, and acks a stop request in
 * our item 616 "master-kernel". (2244: the device tree's qcom,smem = <0x269 0x268> is <inbound outbound>, so 617 is the
 * WPSS's own item and 616 is ours. These are the real numbers, so a swap fails the tests.) */
#include "../modem/QcomSmem.h"
#include "../modem/QcomSmp2p.h"
#define WPSS_ITEM 617u   /* the remote's outbound item: it writes, we read */
#define HOST_ITEM 616u   /* our outbound item: we write, it reads */
static int opt_no_ready,opt_no_handover,opt_no_ack,opt_fatal_ms=-1;
/* 2244: a cold WPSS can take many seconds to say anything (ast208 cut it off at 10 s). ready_ms lets a test make the
 * fake DSP as slow as the real one may be, so the ready timeout is exercised rather than assumed. */
static uint64_t opt_ready_ms=300;
/* 2246 (W3): the edge's options. The fake DSP can withhold its FIFO (480), answer no VERSION, never open IPCRTR, never
 * say HELLO, or announce a service list. */
static int opt_no_glink_fifo,opt_glink_silent,opt_no_open,opt_no_hello,opt_no_intent,opt_no_glink_desc;static unsigned opt_services;
/* 2249 (W5): the receive intent the fake WPSS advertises. The phone announces intents up to 0x4400 (DT qcom,intents),
 * and a board data piece is bounded by the largest idle one, so this is a test parameter rather than a constant. A value
 * that would not fit the model's FIFO is clamped where the message is built. */
static uint32_t opt_intent_size=0x4400u;
/* 2248 (W4): the WLFW service's options, on the same fake peer: announce nothing (so only the lookup finds it), answer no
 * lookup, refuse one message id, withhold the reply to one, truncate one, send no board id. */
static int opt_lookup_silent,opt_lookup_end_marker,opt_wlfw_quiet,opt_no_services;
/* 2249 (W5): the board data download's options. The fake firmware can accept every piece, refuse one by message id, or
 * never send fw_init_done. The files themselves are read from the real modem_a given on the command line, so the sizes
 * and the pieces are the phone's own. */
static int opt_no_init_done,opt_early_init_done;static unsigned opt_refuse_bdf_at=0xffffffffu;static unsigned bdf_pieces_seen;
/* 2250 (W6): the mode stage's options on the same fake peer. A firmware can answer the mode request and then send both
 * indications (the healthy case), answer and send nothing, refuse the mode, send cold_boot_cal_done twice, send fw_ready
 * before cal_done, or volunteer a request_mem indication that ath11k's table says this chip never sends. Each is a way
 * the real firmware can differ from ath11k's expectations, and each must end in a verified stop. */
static int opt_no_cal_done,opt_no_fw_ready,opt_ready_before_cal,opt_repeat_cal,opt_request_mem,opt_refuse_mode;
/* 2251 (W7): the mission half's own controls.
 *   opt_refuse_ini / opt_refuse_cfg   refuse WLAN_INI or WLAN_CFG: the sequence must stop there and still stop cleanly.
 *   opt_refuse_mission                refuse the mission-mode request.
 *   opt_refuse_off                    refuse the mode-off after mission mode: a finding, not a gate, so the stop still has
 *                                     to verify.
 *   opt_no_mission_reply              take the mode request and never answer it.
 *   opt_fatal_after_mission           raise the SMP2P fatal bit a second after mission mode, which is the one thing an
 *                                     idle firmware could do that the host must notice.
 *   opt_mission_indication            volunteer an fw_ready-like indication after mission mode, so the count is exercised. */
static int opt_refuse_ini,opt_refuse_cfg,opt_refuse_mission,opt_refuse_off,opt_no_mission_reply,opt_fatal_after_mission;
static uint32_t opt_mission_indication;
static uint32_t ini_seen,ini_bytes,ini_value,cfg_seen,cfg_bytes,cfg_version_ok,cfg_pipes,cfg_svcs,cfg_shadow;
static int cfg_pipes_ok,cfg_svcs_ok,cfg_shadow_ok;
static uint32_t mission_at_ms,mission_requests,mode_off_requests,mode_off_at_ms;static int mission_replied,mission_taken;
static int off_replied,off_taken;static int fatal_raised;static int cfg_accepted;static int mission_indication_sent;
static uint32_t opt_mode_bytes;      /* the last mode request's payload length, checked against ath11k's 11 */
static uint32_t opt_mode_value=0xffffffffu;  /* the mode the last request asked for */
static uint32_t opt_mode_requests;   /* how many mode requests arrived at all */
static uint32_t opt_cal_ms=200,opt_ready_ms_after_cal=50;  /* the reference port's 6.7 s and 0.1 s, shortened for the model */
static uint32_t mode_replied_ms,mode_at_ms;static int mode_seen,mode_replied,cal_sent,ready_sent;
static uint32_t glink_their_fifo=0,glink_their_bytes=0,glink_desc_off=0;static unsigned glink_their_opens;
static QcomSmp2p remote;static uint64_t remote_started_us;static bool remote_made;
static QcomSmem host_smem(void){return (QcomSmem){(volatile uint8_t *)regions[R_SMEM].va,0x200000u};}
/* 2246 (W3): the fake WPSS's GLINK peer. A real peer in the tests would need a second protocol implementation, so this
 * drives the same shared FIFOs from the other side with the two commands the link's bring-up needs from the DSP:
 * VERSION and OPEN. Everything else (our VERSION_ACK, OPEN_ACK, INTENT) is the code under test. */
enum {RG_VERSION=0x00,RG_VERSION_ACK=0x01,RG_OPEN=0x02,RG_CLOSE=0x03,RG_OPEN_ACK=0x04,RG_INTENT=0x05};
static struct {bool versioned,opened,acked,intented;uint32_t rcid;} rg;
static void rg_tick(uint64_t ms);
static void remote_glink_tick(uint64_t ms){
 if(!glink_their_fifo || opt_glink_silent)return;
 QcomSmem sm=host_smem();QcomSmemPartition part;uint32_t doff=0,dbytes=0;
 if(qcom_smem_partition(&sm,0,13,&part) || qcom_smem_find(&sm,&part,478,&doff,&dbytes))return;
 volatile uint32_t *d=(volatile uint32_t *)((uint8_t *)regions[R_SMEM].va+doff);
 volatile uint8_t *fifo=(volatile uint8_t *)((uint8_t *)regions[R_SMEM].va+glink_their_fifo);
 if(!rg.versioned && ms>=opt_ready_ms+150){
  /* VERSION 1, features 1 (intent reuse). Head/tail are byte offsets; the descriptor's words 2 and 3 are the WPSS's. */
  static const uint8_t m[8]={RG_VERSION,0,1,0,1,0,0,0};
  uint32_t th=d[3];
  for(unsigned i=0;i<8;i++){fifo[th]=m[i];th++;if(th>=glink_their_bytes)th=0;}
  d[3]=th;rg.versioned=true;
 }
 if(rg.versioned && !opt_no_open && !rg.opened && ms>=opt_ready_ms+200){
  uint8_t m[16];
  for(unsigned i=0;i<16;i++)m[i]=0;
  /* 2247: the descriptor's words are {dsp_read, our_write, our_read, dsp_write}. Writing into the DSP's own FIFO moves
   * word 3 (the DSP's write index), which is d[3]. The 2246 comment here said "+0/+4 are theirs", which was the slip the
   * W3 audit caught in note 3; the code was right. */
  m[0]=RG_OPEN;m[2]=0x41;m[3]=0;m[4]=8;m[5]=0;m[6]=0;m[7]=0;   /* lcid 0x41, name length 8 */
  const char *name="IPCRTR";for(unsigned i=0;i<7;i++)m[8+i]=(uint8_t)name[i];
  uint32_t th=d[3];
  for(unsigned i=0;i<16;i++){fifo[th]=m[i];th++;if(th>=glink_their_bytes)th=0;}
  d[3]=th;rg.opened=true;glink_their_opens++;
 }
 /* OPEN_ACK for our lcid 1, which is what makes our side declare the channel up. */
 if(rg.opened && !rg.acked && ms>=opt_ready_ms+250){
  static const uint8_t m[8]={RG_OPEN_ACK,0,1,0,0,0,0,0};
  uint32_t th=d[3];
  for(unsigned i=0;i<8;i++){fifo[th]=m[i];th++;if(th>=glink_their_bytes)th=0;}
  d[3]=th;rg.acked=true;
 }
 /* INTENT: the remote's receive buffer, which is what lets our side send at all. 8-byte header, one pair. */
 if(rg.acked && !opt_no_intent && !rg.intented && ms>=opt_ready_ms+300){
  uint8_t m[16];
  for(unsigned i=0;i<16;i++)m[i]=0;
  m[0]=RG_INTENT;m[2]=0x41;m[3]=0;m[4]=1;m[5]=0;m[6]=0;m[7]=0;    /* one intent */
  /* 2249 (W5): the intent's size is what bounds a board data piece, so a test can make it small (the 0x800 the DT lists
   * as the WPSS's smallest) or large (0x4400, its largest). The phone's own size is logged, never assumed. */
  m[8]=(uint8_t)(opt_intent_size&0xff);m[9]=(uint8_t)((opt_intent_size>>8)&0xff);
  m[10]=(uint8_t)((opt_intent_size>>16)&0xff);m[11]=(uint8_t)((opt_intent_size>>24)&0xff);
  m[12]=1;m[13]=0;m[14]=0;m[15]=0;                                 /* id 1 */
  uint32_t th=d[3];
  for(unsigned i=0;i<16;i++){fifo[th]=m[i];th++;if(th>=glink_their_bytes)th=0;}
  d[3]=th;rg.intented=true;
 }
 rg_tick(ms);
}

/* ---- 2248 (W4): the fake WPSS's WLFW service -----------------------------------------------------------------------
 * The other half of the wire for W4. It reads our FIFO (item 479) and decodes what we wrote byte by byte, with its own
 * constants: it deliberately shares no encoder, decoder or header with the code under test, so a mistake in ours cannot
 * be mirrored in the judge's copy. It writes QRTR control packets and QMI replies in the layouts ath11k's declarations
 * describe. It is not a second QcomQrtr and not a second QcomGlink: it knows the three GLINK commands it needs (OPEN,
 * INTENT, TX_DATA in; TX_DATA and RX_DONE out) and the QRTR packets L1 needs.
 *
 * What it proves: that the bytes our encoder put on the wire are what the documented layout says, and that our client
 * reacts to a well-formed answer. What it cannot prove: anything about the real DSP's behaviour. */
#define RG_WLFW_PORT 0x3001u
#define RG_OUR_PORT 0x4001u       /* the port W4 binds; asserted, not assumed */
#define RG_HDR 32u
enum {RG_Q_DATA=1,RG_Q_HELLO=2,RG_Q_NEW_SERVER=4,RG_Q_NEW_LOOKUP=10};
enum {RG_TX_DATA=0x09,RG_TX_DATA_CONT=0x0c,RG_RX_DONE_W_REUSE=0x0e};
static struct {
 bool hello_sent,announced,txdone_owed,intent_seen,intent_busy;
 uint32_t txdone_id,intent_size,intent_id;
 unsigned packets_in,lookups,requests,malformed,unexpected,qmi_data;
 uint16_t last_message,last_transaction;
 uint8_t last_qmi[160];uint32_t last_qmi_bytes;      /* the last QMI request's bytes, for the encoder checks */
 uint32_t last_lookup_service,last_lookup_instance;
 /* Outbound queue. One intent holds one packet until its RX_DONE arrives, which is what the intent protocol is: a
  * sender with one intent outstanding must not write a second packet into it. Everything the peer wants to say waits
  * here until the intent is free again, so the FIFO can never hold a message the receiver will not accept. */
 uint8_t out[8][160];uint32_t out_bytes[8];unsigned out_head,out_count;
} rgw;
/* Controls. One message id each; 0 means none. */
static uint16_t opt_refuse,opt_silent,opt_short_reply,opt_no_board;
/* 2249 (W5): a board id the firmware does report. 0 means "as the fake always did": 0xff, ath11k's fallback. A real id
 * is what decides whether a variant named for it is looked for, so the test needs to set one. */
static uint32_t opt_board;
/* 2248b (W4 audit defect 1): the ways a firmware can misbehave that the client must survive.
 *   opt_hold_reply / opt_hold_ms  withhold that message's reply and deliver it opt_hold_ms after the request, from the
 *                                 announced port, byte for byte as the request asked for it. With the delay longer than
 *                                 the client's bound it is the LATE reply (the audit's defect 1). 0xffff holds every
 *                                 request, which is the slow-but-answering firmware the dwell clamp exists for
 *                                 (defect 3). Eight slots, so several can be in flight at once.
 *   opt_repeat_reply              send that message's reply twice: the repeated reply after the exchange completed.
 *   opt_wrong_id / opt_wrong_id_for  answer message `for` with header id `wrong_id` at the same transaction: a reply
 *                                 that is not the answer to the outstanding request (audit note 6).
 *   opt_lookup_delay_ms           answer the name service's NEW_LOOKUP this long after it arrived. Used with
 *                                 opt_hold_reply=0xffff to reproduce the audit's ~23 s worst case exactly: a full 3 s
 *                                 lookup plus four replies just inside their own 5 s bounds. */
#define RG_HOLD_SLOTS 8
static uint16_t opt_hold_reply,opt_repeat_reply,opt_wrong_id,opt_wrong_id_for;static uint32_t opt_hold_ms=6000;
static struct {uint8_t bytes[128];uint32_t n,due_ms;} rg_hold[RG_HOLD_SLOTS];static unsigned rg_hold_head,rg_hold_count;
static uint32_t rg_now_ms;   /* the fake WPSS's own clock, set by rg_tick, so a withheld reply can be timed */
static uint32_t opt_lookup_delay_ms;                 /* 0: answer a lookup at once, as a healthy peer does */
static int rg_lookup_pending;static uint32_t rg_lookup_due_ms;

/* Little-endian readers, written out here rather than shared. */
static uint32_t rg_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

/* Writes one GLINK message into the DSP's own FIFO (the one our side reads), 8-byte aligned, moving word 3. 1 on success.
 * It keeps the same 8-byte reserve as a sender so a full FIFO is reported rather than wrapped over. */
static int rg_send_raw(const uint8_t *m,uint32_t bytes)
{
 if(!glink_their_fifo)return 0;
 QcomSmem sm=host_smem();QcomSmemPartition part;uint32_t doff=0,dbytes=0;
 if(qcom_smem_partition(&sm,0,13,&part) || qcom_smem_find(&sm,&part,478,&doff,&dbytes))return 0;
 volatile uint32_t *d=(volatile uint32_t *)((uint8_t *)regions[R_SMEM].va+doff);
 volatile uint8_t *f=(volatile uint8_t *)((uint8_t *)regions[R_SMEM].va+glink_their_fifo);
 const uint32_t n=(bytes+7u)&~7u;
 uint32_t th=d[3],rd=d[2];
 if(th>=glink_their_bytes)return 0;
 const uint32_t room=th>=rd?glink_their_bytes-th+rd:rd-th;
 if(room<=n+8u)return 0;                 /* the same reserve a real sender keeps, so the FIFO is never filled solid */
 for(uint32_t i=0;i<n;i++){f[th]=(i<bytes)?m[i]:0;if(++th==glink_their_bytes)th=0;}
 d[3]=th;
 return 1;
}
/* One QRTR packet, queued for the intent our side advertised. type is the QRTR header's type, which for a control
 * packet IS its command (QcomQrtr's receive rejects a packet whose payload command disagrees with it). */
static int rg_send_qrtr(uint32_t type,uint32_t from_port,uint32_t to_port,const uint8_t *payload,uint32_t bytes)
{
 if(!rgw.intent_seen)return 0;
 const uint32_t padded=(bytes+3u)&~3u;
 if(RG_HDR+padded>512)return 0;
 if(rgw.out_count==8)return 0;                       /* the peer's own backpressure, not the wire's */
 uint8_t *pkt=rgw.out[(rgw.out_head+rgw.out_count)%8];
 for(uint32_t i=0;i<RG_HDR+padded;i++)pkt[i]=0;
 const uint32_t hdr[8]={1,type,13u,from_port,0,bytes,2u,to_port};
 for(unsigned w=0;w<8;w++)for(unsigned i=0;i<4;i++)pkt[4*w+i]=(uint8_t)((hdr[w]>>(8*i))&0xff);
 for(uint32_t i=0;i<bytes;i++)pkt[RG_HDR+i]=payload[i];
 rgw.out_bytes[(rgw.out_head+rgw.out_count)%8]=RG_HDR+padded;
 rgw.out_count++;
 return 1;
}
/* One QRTR control packet: command, then four words, on the control port. */
static int rg_send_control(uint32_t type,const uint32_t words[4])
{
 uint8_t p[20];
 for(unsigned w=0;w<5;w++){const uint32_t v=w?words[w-1]:type;for(unsigned i=0;i<4;i++)p[4*w+i]=(uint8_t)((v>>(8*i))&0xff);}
 return rg_send_qrtr(type,0xfffffffeu,0xfffffffeu,p,20);
}
/* Reads one GLINK message from our FIFO (item 479) into m. 1 when one was read, 0 when there is none. */static int rg_read_our(uint8_t *m,uint32_t *bytes)
{
 QcomSmem sm=host_smem();QcomSmemPartition part;uint32_t doff=0,dbytes=0;
 if(qcom_smem_partition(&sm,0,13,&part) || qcom_smem_find(&sm,&part,478,&doff,&dbytes))return 0;
 uint32_t foff=0,fbytes=0;
 if(qcom_smem_find(&sm,&part,479,&foff,&fbytes))return 0;
 volatile uint32_t *d=(volatile uint32_t *)((uint8_t *)regions[R_SMEM].va+doff);
 volatile uint8_t *f=(volatile uint8_t *)((uint8_t *)regions[R_SMEM].va+foff);
 const uint32_t rd=d[0],wr=d[1];
 if(rd>=fbytes || wr>=fbytes || rd==wr)return 0;
 const uint32_t avail=wr>=rd?wr-rd:fbytes-rd+wr;
 if(avail<8)return 0;
 uint8_t h[16];const uint32_t peek=avail<16?avail:16;
 for(uint32_t i=0;i<peek;i++)h[i]=f[(rd+i)%fbytes];
 uint32_t n;
  if(h[0]==RG_TX_DATA || h[0]==RG_TX_DATA_CONT){if(avail<16)return 0;for(uint32_t i=0;i<16;i++)h[i]=f[(rd+i)%fbytes];n=16u+rg_le32(h+8);}
 else if(h[0]==RG_OPEN){n=8u+((uint32_t)h[4]|(uint32_t)h[5]<<8);}
 else if(h[0]==RG_INTENT){n=8u+8u*((uint32_t)h[4]|(uint32_t)h[5]<<8);}
 else n=8u;
 n=(n+7u)&~7u;
 if(avail<n)return 0;
 const uint32_t copy=n<8192?n:8192;
 for(uint32_t i=0;i<copy;i++)m[i]=f[(rd+i)%fbytes];
 *bytes=copy;
 d[0]=(rd+n)%fbytes;
 return 1;
}

static void rg_put16(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void rg_put32(uint8_t *p,uint32_t v){rg_put16(p,v);rg_put16(p+2,v>>16);}
static void rg_put64(uint8_t *p,uint64_t v){rg_put32(p,(uint32_t)v);rg_put32(p+4,(uint32_t)(v>>32));}
static uint32_t rg_tlv(uint8_t *out,uint32_t at,uint8_t type,const uint8_t *v,uint32_t n)
{
 out[at]=type;rg_put16(out+at+1,n);for(uint32_t i=0;i<n;i++)out[at+3+i]=v?v[i]:0;return at+3+n;
}

/* ---- 2251 (W7): the judge's own copy of ath11k's tables ------------------------------------------------
 * Written out here, from ath11k's declarations, so the peer compares our WLAN_CFG against something other than the header
 * the code under test read. A shared misreading would pass a test that compared our table with itself; these are the same
 * numbers typed twice, deliberately, and a change to either one fails the test.
 *
 * ce_truth is ath11k_target_ce_config_wlan_qca6390 (hw.c): {pipe_num, pipe_dir, nentries, nbytes_max, flags}.
 * pipe_dir is relative to the host: 1 = IN, 2 = OUT, 3 = INOUT, 4 = INOUT_H2H. flags is CE_ATTR_FLAGS | CE_ATTR_DIS_INTR
 * = 8 on CE4 and CE7 and 0 elsewhere on a little-endian host. */
static const uint32_t ce_truth[9][5]={
 {0,2,32,2048,0},{1,1,32,2048,0},{2,1,32,2048,0},{3,2,32,2048,0},{4,2,256,256,8},
 {5,1,32,2048,0},{6,3,32,16384,0},{7,4,0,0,8},{8,3,32,16384,0},
};
/* svc_truth is ath11k_target_service_to_ce_map_wlan_qca6390 (hw.c): {service_id, pipe_dir, pipe_num}. The ids are
 * htc.h's SVC(group, idx) = (group << 8) | idx: 0x0104 VO, 0x0102 BK, 0x0101 BE, 0x0103 VI, 0x0100 WMI_CONTROL,
 * 0x0001 RSVD_CTRL, 0x0300 HTT_DATA_MSG. */
static const uint32_t svc_truth[14][3]={
 {0x0104,2,3},{0x0104,1,2},{0x0102,2,3},{0x0102,1,2},{0x0101,2,3},{0x0101,1,2},{0x0103,2,3},
 {0x0103,1,2},{0x0100,2,3},{0x0100,1,2},{0x0001,2,0},{0x0001,1,2},{0x0300,2,4},{0x0300,1,1},
};
/* shadow_truth is ath11k_ce_shadow_config's own list for this chip: the HP (or TP, for a destination ring) word of every
 * CE ring the host builds. CE0/3/4/7 have source rings, CE1/2/5 have destination rings and so contribute their TP at
 * 0x400 and their status ring's TP at 0x408 + 4. wcn6750_regs puts CE0_SRC at 0x01b80000 and CE0_DST at 0x01b81000,
 * 0x2000 apart. */
static const uint32_t shadow_truth[10]={
 0x01b80400u,                                      /* CE0 src HP */
 0x01b83400u,0x01b8340cu,                          /* CE1 dst TP, CE1 dst status TP */
 0x01b85400u,0x01b8540cu,                          /* CE2 dst TP, CE2 dst status TP */
 0x01b86400u,                                      /* CE3 src HP */
 0x01b88400u,                                      /* CE4 src HP */
 0x01b8b400u,0x01b8b40cu,                          /* CE5 dst TP, CE5 dst status TP */
 0x01b8e400u,                                      /* CE7 src HP */
};
static const uint32_t shadow_truth_count=10u;

/* One QMI request from us, parsed by hand: 7-byte header, then TLVs. Answered unless this test withholds it. */
/* 2249 (W5): one board data piece, checked and answered by the peer below, and the reply builder both it and the
 * single-reply path use. Declared here because the request dispatcher is above them. */
static void bdf_peer_piece(const uint8_t *q,uint32_t bytes,uint16_t transaction);
static uint32_t rg_qmi_reply(uint8_t *out,uint32_t cap,uint16_t transaction,uint16_t message,uint16_t result,
                             uint16_t error);
static void rg_our_request(const uint8_t *q,uint32_t bytes)
{
 uint8_t type=q[0];const uint16_t transaction=(uint16_t)(q[1]|q[2]<<8);
 const uint16_t message=(uint16_t)(q[3]|q[4]<<8);const uint32_t length=(uint32_t)(q[5]|q[6]<<8);
 if(length!=bytes-7){rgw.malformed++;return;}
 for(uint32_t at=7;at<bytes;){const uint32_t n=(uint32_t)(q[at+1]|q[at+2]<<8);if(bytes-at-3<n){rgw.malformed++;return;}at+=3+n;}
 rgw.qmi_data++;rgw.requests++;rgw.last_message=message;rgw.last_transaction=transaction;
 rgw.last_qmi_bytes=bytes<sizeof(rgw.last_qmi)?bytes:(uint32_t)sizeof(rgw.last_qmi);
 for(uint32_t i=0;i<rgw.last_qmi_bytes;i++)rgw.last_qmi[i]=q[i];
 if(type!=0){rgw.unexpected++;return;}
 if(message==opt_silent)return;
 /* 2249 (W5): the board data download goes to the peer that reassembles and checks the file. It is validated here even
  * when its reply is being held, because the reply's timing must not change what the firmware "thinks": a held piece is
  * still a piece the firmware accepted. */
 if(message==0x0025){bdf_peer_piece(q,bytes,transaction);return;}
 /* 2251 (W7): WLAN_INI. One TLV, 0x10, one byte: enablefwlog. The 4-byte payload is ath11k's
  * QMI_WLANFW_WLAN_INI_REQ_MSG_V01_MAX_LEN, and the value must be 1 (log on), because that is what ath11k sends when
  * fw_wmi_diag_event is set, which core.c sets for this chip. */
 if(message==0x002f){
  uint32_t on=0xffffffffu;
  for(uint32_t at=7;at<bytes;){const uint32_t n=(uint32_t)q[at+1]|(uint32_t)q[at+2]<<8;if(bytes-at-3<n)break;
   if(q[at]==0x10 && n==1)on=q[at+3];
   at+=3+n;}
  ini_seen=1;ini_bytes=bytes-7;ini_value=on;
  uint8_t reply[32];
  /* A malformed INI (wrong length, missing TLV, value not 1) is refused, exactly as the real firmware refuses what it
   * cannot read: the test's own assertions then fail on the refusal rather than on a shared constant. */
  const uint32_t rn=rg_qmi_reply(reply,sizeof reply,transaction,0x002f,
                                 (on==1 && ini_bytes==4 && !opt_refuse_ini)?0u:1u,(on==1 && ini_bytes==4 && !opt_refuse_ini)?0u:0x30u);
  if(rn)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  if(rn && opt_repeat_reply==0x002f)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  return;
 }
 /* 2251 (W7): WLAN_CFG. This is the one message whose content is a description of hardware, so the peer decodes it fully
  * and checks it against ath11k's tables rather than agreeing with whatever arrived: the host_version string, the pipe
  * count and each pipe's five fields, the service count and each service's three fields, and the shadow-register count and
  * every address. A host that sent a wrong pipe direction, a wrong shadow address or a truncated array is caught here. */
 if(message==0x0023){
  uint32_t version_ok=0,pipe_count=0xffffffffu,svc_count=0xffffffffu,shadow_count=0xffffffffu,shape_ok=1;
  uint32_t pipes[12][5],svcs[24][3],shadow[36];
  memset(pipes,0,sizeof pipes);memset(svcs,0,sizeof svcs);memset(shadow,0,sizeof shadow);
  for(uint32_t at=7;at<bytes;){
   const uint32_t n=(uint32_t)q[at+1]|(uint32_t)q[at+2]<<8;if(bytes-at-3<n){shape_ok=0;break;}
   const uint8_t *v=q+at+3;
   if(q[at]==0x10){ /* host_version: a plain string, no count */
    if(n==3 && !memcmp(v,"WIN",3))version_ok=1;
   } else if(q[at]==0x11){
    if(n<1){shape_ok=0;} else {
     pipe_count=v[0];
     if(n!=1u+20u*pipe_count)shape_ok=0;
     for(uint32_t i=0;i<pipe_count && i<12;i++)for(unsigned w=0;w<5;w++)
      pipes[i][w]=(uint32_t)v[1+20*i+4*w]|(uint32_t)v[2+20*i+4*w]<<8|(uint32_t)v[3+20*i+4*w]<<16|(uint32_t)v[4+20*i+4*w]<<24;
    }
   } else if(q[at]==0x12){
    if(n<1){shape_ok=0;} else {
     svc_count=v[0];
     if(n!=1u+12u*svc_count)shape_ok=0;
     for(uint32_t i=0;i<svc_count && i<24;i++)for(unsigned w=0;w<3;w++)
      svcs[i][w]=(uint32_t)v[1+12*i+4*w]|(uint32_t)v[2+12*i+4*w]<<8|(uint32_t)v[3+12*i+4*w]<<16|(uint32_t)v[4+12*i+4*w]<<24;
    }
   } else if(q[at]==0x14){
    if(n<1){shape_ok=0;} else {
     shadow_count=v[0];
     if(n!=1u+4u*shadow_count)shape_ok=0;
     for(uint32_t i=0;i<shadow_count && i<36;i++)
      shadow[i]=(uint32_t)v[1+4*i]|(uint32_t)v[2+4*i]<<8|(uint32_t)v[3+4*i]<<16|(uint32_t)v[4+4*i]<<24;
    }
   } else if(q[at]==0x13)shape_ok=0;   /* the older shadow_reg, which ath11k never sends */
   at+=3+n;
  }
  cfg_seen=1;cfg_bytes=bytes-7;cfg_version_ok=version_ok;cfg_pipes=pipe_count;cfg_svcs=svc_count;cfg_shadow=shadow_count;
  /* Compare against the tables the test knows independently, written out in the test rather than read from the header:
   * a peer that read the same table the code under test read would agree with a shared misreading. */
  cfg_pipes_ok=shape_ok && pipe_count==9;
  for(uint32_t i=0;cfg_pipes_ok && i<9;i++)for(unsigned w=0;w<5;w++)if(pipes[i][w]!=ce_truth[i][w])cfg_pipes_ok=0;
  cfg_svcs_ok=shape_ok && svc_count==14;
  for(uint32_t i=0;cfg_svcs_ok && i<14;i++)for(unsigned w=0;w<3;w++)if(svcs[i][w]!=svc_truth[i][w])cfg_svcs_ok=0;
  cfg_shadow_ok=shape_ok && shadow_count==shadow_truth_count;
  for(uint32_t i=0;cfg_shadow_ok && i<shadow_truth_count;i++)if(shadow[i]!=shadow_truth[i])cfg_shadow_ok=0;
  uint8_t reply[32];
  const uint32_t rn=rg_qmi_reply(reply,sizeof reply,transaction,0x0023,(cfg_pipes_ok && cfg_svcs_ok && cfg_shadow_ok && cfg_version_ok && !opt_refuse_cfg)?0u:1u,
                                 (cfg_pipes_ok && cfg_svcs_ok && cfg_shadow_ok && cfg_version_ok && !opt_refuse_cfg)?0u:0x30u);
  if(rn)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  if(rn && opt_repeat_reply==0x0023)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  /* 2251: an accepted configuration is what a mission request is only legal after, so the peer remembers it and refuses a
   * mission request that arrives first. That is the order ath11k_qmi_firmware_start sends, and the stage has to keep it. */
  if(rn && !opt_refuse_cfg && cfg_pipes_ok && cfg_svcs_ok && cfg_shadow_ok && cfg_version_ok)cfg_accepted=1;
  return;
 }
 /* 2250 (W6): the cold-boot mode request. Its payload is ath11k's own 11 bytes: TLV 0x01 a u32 mode, TLV 0x10 a u8
  * hw_debug. The fake firmware reads the mode out of the request rather than remembering what our code was supposed to
  * send, so a wrong mode, a missing hw_debug TLV or the wrong length is caught here and not by a shared constant. */
 if(message==0x0022){
  /* 2251 (W7): opt_mode_requests and mode_seen are about the cold-boot request, which is what W6's assertions and its
   * peer timeline are about; mission mode and the mode-off are counted separately. Counted before the mode is decoded,
   * because ath11k's own "did the host ask at all" question is about the message, not its payload. */
  uint32_t peek=0xffffffffu;
  for(uint32_t at=7;at<bytes;){const uint32_t n=(uint32_t)q[at+1]|(uint32_t)q[at+2]<<8;if(bytes-at-3<n)break;
   if(q[at]==0x01 && n==4){peek=rg_le32(q+at+3);break;}
   at+=3+n;}
  if(peek==7u || peek==0xffffffffu){opt_mode_requests++;mode_seen=1;mode_at_ms=rg_now_ms;}
  uint32_t mode=0xffffffffu,have_mode=0,have_debug=0,debug=0xffffffffu;
  for(uint32_t at=7;at<bytes;){const uint32_t n=(uint32_t)q[at+1]|(uint32_t)q[at+2]<<8;if(bytes-at-3<n)break;
   if(q[at]==0x01 && n==4){mode=rg_le32(q+at+3);have_mode=1;}
   if(q[at]==0x10 && n==1){debug=q[at+3];have_debug=1;}
   at+=3+n;}
  opt_mode_bytes=bytes-7;
  /* 2251: opt_mode_value is the mode the cold-boot request asked for, which is what W6's assertions are about. Mission
   * mode and the mode-off are their own counters, so the three cannot overwrite each other. */
  if(mode==7u)opt_mode_value=mode;
  /* 2251 (W7): the same message id carries three modes: 7 cold boot, 0 mission, 4 off. The peer answers each with what it
   * would really answer, because "the firmware took mission mode" and "the firmware came out of it" are different
   * statements and the stage has to report them apart. Mode 4 before mode 0 is refused with error 48, which is exactly
   * what the reference port's firmware did; that is the case the stage's own rule exists to avoid sending. */
  if(mode==0u && have_mode){
   mission_requests++;mission_at_ms=rg_now_ms;
   uint8_t reply[32];
   /* 2251: a mission request before the configuration was accepted is a protocol error, and the peer refuses it with
    * error 48, which is the class of refusal ath11k's own error handling would surface. */
   const uint32_t ok=cfg_accepted && have_debug && !debug && !opt_refuse_mission && !opt_no_mission_reply;
   if(opt_no_mission_reply)return;
   const uint32_t rn=rg_qmi_reply(reply,sizeof reply,transaction,0x0022,ok?0u:1u,ok?0u:0x30u);
   if(rn)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
   if(rn && opt_repeat_reply==0x0022)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
   mission_replied=1;if(ok)mission_taken=1;
   /* 2252: a firmware that DMAs on a stream with no SMR would leave USF in sGFSR. opt_usf_in_mission makes it so. */
   if(ok && opt_usf_in_mission){uint32_t g=2;memcpy((void *)(regions[R_SMMU].va+0x48),&g,4);}
   return;
  }
  if(mode==4u && have_mode){
   mode_off_requests++;mode_off_at_ms=rg_now_ms;
   uint8_t reply[32];
   /* Before mission mode the firmware refuses the mode-off with error 48, as the reference port's did; after it, it takes
    * it. opt_refuse_off forces the refusal so the stage's "a finding, not a gate" path is exercised. */
   const uint32_t ok=mission_taken && have_debug && !debug && !opt_refuse_off;
   const uint32_t rn=rg_qmi_reply(reply,sizeof reply,transaction,0x0022,ok?0u:1u,ok?0u:0x30u);
   if(rn)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
   off_replied=1;if(ok)off_taken=1;
   return;
  }
  uint8_t reply[32];
  const uint32_t rn=rg_qmi_reply(reply,sizeof reply,transaction,0x0022,(have_mode && have_debug && !debug && opt_refuse_mode)?1u:0u,
                                 (have_mode && have_debug && !debug && opt_refuse_mode)?0x30u:0u);
  /* A malformed mode request is a failure of the code under test, not of the firmware: the peer refuses it and the
   * refusal is what the test then sees. */
  if(!have_mode || !have_debug || debug!=0 || (opt_mode_bytes!=11 && opt_mode_bytes!=7)){
   const uint32_t bad=rg_qmi_reply(reply,sizeof reply,transaction,0x0022,1,0x30);
   if(bad)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,bad);
   return;
  }
  if(rn)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  /* 2250 (W6): the same reply a second time, queued behind the first, exactly as W4's repeated reply is. This is the only
   * way to hand the client two replies that share a transaction and an id, so it is what the reply guard's upper bound is
   * tested with: the second must be dropped, because by then the step is past the last one that admits a reply. */
  if(rn && opt_repeat_reply==0x0022)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,rn);
  if(!opt_refuse_mode)mode_replied=1;
  return;
 }
 if(message==opt_hold_reply || opt_hold_reply==0xffffu){
  /* 2248b: hold the reply and send it opt_hold_ms later, from the announced port. Built now, with this request's own
   * transaction and id, so it is exactly the reply the client asked for — only late. */
  uint8_t held[128];
  const uint32_t hn=rg_qmi_reply(held,sizeof(held),transaction,message,0,0);
  if(hn && rg_hold_count<RG_HOLD_SLOTS){
   const unsigned slot=(rg_hold_head+rg_hold_count++)%RG_HOLD_SLOTS;
   memcpy(rg_hold[slot].bytes,held,hn);rg_hold[slot].n=hn;rg_hold[slot].due_ms=rg_now_ms+opt_hold_ms;
  }
  return;
 }
 uint8_t reply[128];
 const uint16_t result=(uint16_t)(message==opt_refuse?1u:0u);
 const uint16_t error=(uint16_t)(result?0x30u:0u);
 const uint16_t answered_id=(uint16_t)(message==opt_wrong_id_for?opt_wrong_id:message);
 uint32_t n=rg_qmi_reply(reply,sizeof(reply),transaction,answered_id,result,error);
 if(opt_short_reply==message && n>7) n=9;      /* a TLV that claims more than it carries: must be rejected */
 if(!n)return;
 rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,n);
 if(opt_repeat_reply==message){
  /* 2248b: the same reply a second time, queued behind the first. On the phone this is a firmware that answers twice;
   * the client has already advanced by then, so the second must be dropped rather than advance it again. */
  rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,reply,n);
 }
}
/* ---- 2249 (W5): the board data download, decoded and reassembled by hand -----------------------------------------
 * The peer's job here is to be what the firmware is: take pieces, check they are the ones a sender of the file would
 * have produced, and only then say init_done. So it keeps its own copy of each file's bytes, compares every piece
 * against the file at the offset the pieces so far imply, and refuses a piece whose seg_id, total_size, file_id, end flag
 * or bdf_type is not what the sequence demands. That catches a sender that repeats a piece, skips one, sends them out of
 * order, gets the total wrong, or mislabels the file's type — none of which our own encoder can notice.
 *
 * Its own copy comes from the same modem_a file the code under test reads, opened separately: a test that only compared
 * our bytes with our bytes would pass on a shared misreading. */
static struct {
 uint8_t data[0x10000];uint32_t bytes;   /* the file being sent, reassembled */
 uint32_t file_id, expected_offset, pieces;uint8_t type;int started,failed,complete;
 uint32_t init_due_ms;                   /* when init_done is due, set on the last piece */
 int init_sent,regdb_done,board_done,init_pending;   /* the indication queued once, which file finished, board's end seen */
 char what[16];
} rbf;
static FILE *bdf_file_handle;   /* a second handle on modem_a, so the peer reads it independently */

/* Reads image/qca6750/<name> out of the FAT16 modem_a image, with a reader written here and not shared with the code
 * under test, so a bug in our firmware reader cannot be mirrored in the judge. The names on this volume are long ones
 * (regdb.bin is stored as a VFAT long name over a mangled 8.3 alias), so matching walks the long-name entries: the
 * checksum in the short entry is what ties them together, which is the only reliable way, and it is exactly what the
 * firmware reader does not do for the files it opens (it uses plain 8.3 for those). */
static uint8_t bdf_lfn_sum(const uint8_t *e)
{
 uint8_t sum=0;
 for(unsigned i=0;i<11;i++)sum=(uint8_t)(((sum&1)<<7)+(sum>>1))+e[i];
 return sum;
}
/* Appends the characters of one long-name entry, in the order they appear, skipping the padded tail. */
static uint32_t bdf_lfn_text(const uint8_t *e,char *out,uint32_t cap)
{
 static const uint8_t at[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 uint32_t n=0;
 for(unsigned i=0;i<13;i++){
  const uint32_t c=(uint32_t)e[at[i]]|(uint32_t)e[at[i]+1]<<8;
  if(c==0x0000 || c==0xffff)continue;
  if(n+1>=cap)break;
  out[n++]=(c<0x80)?(char)c:'?';
 }
 out[n]=0;
 return n;
}
static int bdf_peer_file(const char *name,uint8_t *out,uint32_t capacity,uint32_t *bytes)
{
 if(!bdf_file_handle)return -1;
 uint8_t boot[512];
 if(fseek(bdf_file_handle,0,SEEK_SET) || fread(boot,1,sizeof boot,bdf_file_handle)!=sizeof boot)return -1;
 if(boot[510]!=0x55 || boot[511]!=0xaa)return -1;
 const uint32_t bps=(uint32_t)boot[11]|(uint32_t)boot[12]<<8, spc=boot[13];
 const uint32_t reserved=(uint32_t)boot[14]|(uint32_t)boot[15]<<8, nfats=boot[16];
 const uint32_t rootent=(uint32_t)boot[17]|(uint32_t)boot[18]<<8, fatsz=(uint32_t)boot[22]|(uint32_t)boot[23]<<8;
 const uint32_t small=(uint32_t)boot[19]|(uint32_t)boot[20]<<8;
 const uint32_t total=small?small:((uint32_t)boot[32]|(uint32_t)boot[33]<<8|(uint32_t)boot[34]<<16|(uint32_t)boot[35]<<24);
 const uint64_t fat_off=(uint64_t)reserved*bps, root_off=fat_off+(uint64_t)nfats*fatsz*bps;
 const uint64_t root_sectors=((uint64_t)rootent*32+bps-1)/bps;
 const uint64_t data_off=root_off+root_sectors*bps, cl_bytes=(uint64_t)spc*bps;
 const uint32_t ncl=(uint32_t)(((uint64_t)total*bps-data_off)/cl_bytes);
 uint8_t *fat=malloc((size_t)fatsz*bps);
 if(!fat)return -1;
 if(fseek(bdf_file_handle,(long)fat_off,SEEK_SET) || fread(fat,1,(size_t)fatsz*bps,bdf_file_handle)!=(size_t)fatsz*bps){free(fat);return -1;}

 /* Walks the path one component at a time, keeping a directory's bytes in a heap buffer of exactly its clusters. */
 uint32_t cl=0;int is_dir=1;
 for(const char *want=name;*want;){
  const char *slash=strchr(want,'/');
  const size_t want_len=slash?(size_t)(slash-want):strlen(want);
  uint8_t *dir=malloc(cl_bytes*(uint64_t)ncl);
  uint32_t dir_bytes=0;
  if(!dir){free(fat);return -1;}
  if(!is_dir){free(dir);free(fat);return -1;}
  if(cl==0){
   dir_bytes=(uint32_t)rootent*32;
   if(fseek(bdf_file_handle,(long)root_off,SEEK_SET) || fread(dir,1,dir_bytes,bdf_file_handle)!=dir_bytes){free(dir);free(fat);return -1;}
  } else {
   uint32_t c=cl,guard=0;
   while(c>=2 && c<ncl+2 && c<0xfff0 && guard++<ncl && dir_bytes+(uint32_t)cl_bytes<=(uint32_t)(cl_bytes*(uint64_t)ncl)){
    if(fseek(bdf_file_handle,(long)(data_off+(uint64_t)(c-2)*cl_bytes),SEEK_SET)){free(dir);free(fat);return -1;}
    if(fread(dir+dir_bytes,1,(size_t)cl_bytes,bdf_file_handle)!=(size_t)cl_bytes){free(dir);free(fat);return -1;}
    dir_bytes+=(uint32_t)cl_bytes;
    c=(uint32_t)fat[c*2]|(uint32_t)fat[c*2+1]<<8;
   }
  }
  /* One component: prefer an exact long-name match whose checksum the short entry confirms; fall back to a plain 8.3
   * match for a name that has no long form. */
  char lfn[260]={0};uint32_t lfn_len=0;uint8_t lfn_sum=0;int have_lfn=0;
  int found=0;uint32_t next_cl=0,file_bytes=0;uint8_t entry_dir=0;
  for(uint32_t at=0;at+32<=dir_bytes;at+=32){
   const uint8_t *e=dir+at;
   if(e[0]==0)break;
   if(e[0]==0xe5){lfn_len=0;have_lfn=0;continue;}
   if(e[11]==0x0f){
    /* The entries arrive in reverse order, each carrying the sum of the short entry they belong to. Collecting them
     * backwards means the first text written is the tail of the name, so this joins them in a fixed array by ordinal. */
    char text[16];bdf_lfn_text(e,text,sizeof text);
    const uint32_t ord=(uint32_t)e[0]&0x1f;
    if(ord>=1 && ord<=20){
     const uint32_t base=(ord-1)*13;
     if(base+strlen(text)<sizeof lfn){memcpy(lfn+base,text,strlen(text));if(base+strlen(text)+1>lfn_len)lfn_len=(uint32_t)(base+strlen(text));}
     lfn_sum=e[13];have_lfn=1;
    }
    continue;
   }
   if(e[11]&0x08){continue;}
   int match=0;
   if(have_lfn && bdf_lfn_sum(e)==lfn_sum && lfn_len==want_len && !memcmp(lfn,want,want_len))match=1;
   if(!match && !have_lfn && want_len<=12){
    char want83[11];size_t wl=0,i=0;
    while(want[i] && want[i]!='.' && wl<8){want83[wl++]=(want[i]>='a'&&want[i]<='z')?(char)(want[i]-32):want[i];i++;}
    while(wl<8)want83[wl++]=' ';
    if(want[i]=='.')i++;
    for(unsigned k=0;k<3;k++){want83[wl++]=(i<want_len)?((want[i]>='a'&&want[i]<='z')?(char)(want[i]-32):want[i]):' ';if(i<want_len)i++;}
    match=!memcmp(e,want83,11);
   }
   lfn_len=0;have_lfn=0;
   if(!match)continue;
   next_cl=(uint32_t)e[26]|(uint32_t)e[27]<<8;
   file_bytes=(uint32_t)e[28]|(uint32_t)e[29]<<8|(uint32_t)e[30]<<16|(uint32_t)e[31]<<24;
   entry_dir=(e[11]&0x10)!=0;found=1;break;
  }
  free(dir);
  if(!found){free(fat);return -1;}
  is_dir=entry_dir;cl=next_cl;
  if(slash){
   if(!is_dir){free(fat);return -1;}   /* a path component that is not a directory */
   want=slash+1;
   continue;
  }
  if(is_dir || file_bytes>capacity){free(fat);return -1;}
  /* The file's chain, read cluster by cluster, stopping at its recorded size. */
  uint32_t done=0,c=cl,guard=0;
  while(done<file_bytes){
   if(!(c>=2 && c<ncl+2 && c<0xfff0) || guard++>ncl){free(fat);return -1;}
   const uint32_t take=(uint32_t)((file_bytes-done)<cl_bytes?file_bytes-done:cl_bytes);
   if(fseek(bdf_file_handle,(long)(data_off+(uint64_t)(c-2)*cl_bytes),SEEK_SET)){free(fat);return -1;}
   if(fread(out+done,1,take,bdf_file_handle)!=take){free(fat);return -1;}
   done+=take;
   c=(uint32_t)fat[c*2]|(uint32_t)fat[c*2+1]<<8;
  }
  *bytes=file_bytes;free(fat);return 0;
 }
 free(fat);
 return -1;
}
/* One BDF_DOWNLOAD request from us, checked against the file the peer holds. Any disagreement is remembered, and the
 * reply carries a refusal so the code under test sees the firmware say no rather than hanging. */
static void bdf_peer_piece(const uint8_t *q,uint32_t bytes,uint16_t transaction)
{
 uint32_t file_id=0,total=0,seg=0,type=0,end=0;const uint8_t *data=0;uint32_t data_bytes=0;
 int have_id=0,have_total=0,have_seg=0,have_data=0,have_end=0,have_type=0,have_valid=0;
 for(uint32_t at=7;at<bytes;){
  const uint32_t n=(uint32_t)q[at+1]|(uint32_t)q[at+2]<<8;
  if(bytes-at-3<n){rbf.failed++;break;}
  const uint8_t *v=q+at+3;
  switch(q[at]){
  case 0x01: have_valid=n==1&&v[0]==1;break;
  case 0x10: if(n==4){file_id=rg_le32(v);have_id=1;}else rbf.failed++;break;
  case 0x11: if(n==4){total=rg_le32(v);have_total=1;}else rbf.failed++;break;
  case 0x12: if(n==4){seg=rg_le32(v);have_seg=1;}else rbf.failed++;break;
  case 0x13: if(n>=2){const uint32_t count=(uint32_t)v[0]|(uint32_t)v[1]<<8;
             if(count<=n-2){data=v+2;data_bytes=count;have_data=1;}else rbf.failed++;}else rbf.failed++;break;
  case 0x14: if(n==1){end=v[0];have_end=1;}else rbf.failed++;break;
  case 0x15: if(n==1){type=v[0];have_type=1;}else rbf.failed++;break;
  default: break;
  }
  at+=3+n;
 }
 if(!have_valid || !have_id || !have_total || !have_seg || !have_data || !have_end || !have_type){rbf.failed++;goto refuse;}
 if(!rbf.started || seg==0){
  /* First piece: pick the file from the id the sender used, exactly as the vendor driver would have named it. */
  const char *name=(type==1)?"bdwlan.elf":"bdwlan.bin";
  (void)name;
  rbf.bytes=0;rbf.expected_offset=0;rbf.pieces=0;rbf.file_id=file_id;rbf.type=(uint8_t)type;rbf.started=1;
  const char *what=type==4?"regdb":"board";
  snprintf(rbf.what,sizeof rbf.what,"%s",what);
  uint32_t file_bytes=0;
  const char *path=type==4?"image/qca6750/regdb.bin":(file_id==0xff?"image/qca6750/bdwlan.bin":"");
  if(!path[0]){rbf.failed++;goto refuse;}
  if(bdf_peer_file(path,rbf.data,sizeof rbf.data,&file_bytes)){rbf.failed++;goto refuse;}
  rbf.bytes=file_bytes;
 }
 if(seg!=rbf.pieces){rbf.failed++;goto refuse;}                    /* a repeated or skipped piece */
 if(file_id!=rbf.file_id || type!=rbf.type){rbf.failed++;goto refuse;}
 if(total!=rbf.bytes-rbf.expected_offset){rbf.failed++;goto refuse;}  /* total_size is what is LEFT */
 if(data_bytes>rbf.bytes-rbf.expected_offset){rbf.failed++;goto refuse;}
 if(memcmp(data,rbf.data+rbf.expected_offset,data_bytes)){rbf.failed++;goto refuse;}
 if(end!=(rbf.expected_offset+data_bytes>=rbf.bytes)){rbf.failed++;goto refuse;}
 rbf.expected_offset+=data_bytes;rbf.pieces++;
 if(end){
  rbf.complete++;
  if(rbf.type==4)rbf.regdb_done=1; else {rbf.board_done=1;rbf.init_pending=1;}
 }
 if(opt_refuse_bdf_at==rbf.pieces-1){rbf.failed++;goto refuse;}
 bdf_pieces_seen++;
 {uint8_t r[32];const uint32_t n=rg_qmi_reply(r,sizeof r,transaction,0x0025,0,0);
  if(!n)return;
  /* 2249: the reply can be held, which is how a slow-but-answering firmware is modelled. The piece was still checked and
   * counted above, so the only thing the delay changes is when the sender hears about it. init_done is timed from the
   * reply actually going out, not from the piece arriving: a firmware cannot have finished with a file it has not yet
   * acknowledged, and the model has to be that truthful or the code under test gets blamed for its own honesty. */
  if(opt_hold_reply==0x0025 || opt_hold_reply==0xffffu){
   if(rg_hold_count<RG_HOLD_SLOTS){
    const unsigned slot=(rg_hold_head+rg_hold_count++)%RG_HOLD_SLOTS;
    memcpy(rg_hold[slot].bytes,r,n);rg_hold[slot].n=n;rg_hold[slot].due_ms=rg_now_ms+opt_hold_ms;
    if(end)rbf.init_due_ms=rg_hold[slot].due_ms+50u;
   }
   return;
  }
  if(end)rbf.init_due_ms=rg_now_ms+50u;
  rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,n);}
 return;
refuse:
 {uint8_t r[32];const uint32_t n=rg_qmi_reply(r,sizeof r,transaction,0x0025,1,0x30);
  if(n)rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,n);}
}
/* init_done, once both files are in. On the phone this arrives when the firmware has taken the board data; a firmware
 * that refuses a piece never sends it, which is what the negative control needs. */
static void bdf_peer_maybe_init(uint32_t now_ms)
{
 /* 2249: a firmware that speaks out of turn. With opt_early_init_done the indication goes out once the REGULATORY database
  * is in and the board file has only just started, which is a real thing a buggy or confused firmware can do; the host
  * must ignore it and wait for the one that follows the board file. */
 if(opt_no_init_done || rbf.failed)return;
 if(opt_early_init_done){
  /* Two indications: one once regdb is in (too early, and the host must ignore it) and one after the board file. */
  const int stage=rbf.board_done?2:(rbf.regdb_done?1:0);
  if(!stage || rbf.init_sent>=stage)return;
  if(!rbf.init_due_ms)rbf.init_due_ms=now_ms+50u;
  if(now_ms<rbf.init_due_ms)return;
  uint8_t r[7]={4,9,0,0x38,0,0,0};
  rg_put16(r+1,9);rg_put16(r+3,0x0038);
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,sizeof r)){rbf.init_sent=stage;rbf.init_due_ms=rbf.board_done?0:now_ms+50u;}
  return;
 }
 if(rbf.init_sent || !rbf.board_done)return;
 if(now_ms<rbf.init_due_ms)return;
 uint8_t r[7]={4,9,0,0x38,0,0,0};
 rg_put16(r+1,9);rg_put16(r+3,0x0038);
 if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,sizeof r))rbf.init_sent=1;
}
/* A reply to one request: header, result TLV, then whatever that message's reply carries. 7 + 7 + ... */
static uint32_t rg_qmi_reply(uint8_t *out,uint32_t cap,uint16_t transaction,uint16_t message,uint16_t result,uint16_t error)
{
 if(cap<14)return 0;
 out[0]=2;rg_put16(out+1,transaction);rg_put16(out+3,message);rg_put16(out+5,0);
 uint8_t r[4];rg_put16(r,result);rg_put16(r+2,error);
 uint32_t at=rg_tlv(out,7,0x02,r,4);
 /* The length goes in now and is rewritten at the end: a refusal returns early and must still carry a valid one. */
 rg_put16(out+5,at-7);
 if(result)return at;
 if(message==0x0020){                       /* ind_register: fw_status, TLV 0x10, eight bytes */
  uint8_t st[8];rg_put64(st,0x1122334455667788ull);at=rg_tlv(out,at,0x10,st,8);
 } else if(message==0x0024){                /* cap: chip 0x10, board 0x11, soc 0x12, fw version 0x13 */
  uint8_t chip[8];rg_put32(chip,1);rg_put32(chip+4,0xb);at=rg_tlv(out,at,0x10,chip,8);
  if(!opt_no_board){uint8_t b[4];rg_put32(b,opt_board?opt_board:0xffu);at=rg_tlv(out,at,0x11,b,4);}
  uint8_t soc[4];rg_put32(soc,0x1234);at=rg_tlv(out,at,0x12,soc,4);
  uint8_t fw[36];memset(fw,0,sizeof(fw));rg_put32(fw,0x10056);memcpy(fw+4,"2026-01-01 00:00:00",19);
  at=rg_tlv(out,at,0x13,fw,sizeof(fw));
 } else if(message==0x004c){                /* device_info: bar address 0x10, bar size 0x11 */
  uint8_t a[8];rg_put64(a,0x0bad0000ull);at=rg_tlv(out,at,0x10,a,8);
  uint8_t s[4];rg_put32(s,0x200000u);at=rg_tlv(out,at,0x11,s,4);
 }
 if(at<7 || at-7>0xffffu)return 0;
 rg_put16(out+5,at-7);
 return at;
}
/* One QRTR packet from us. Its header is decoded here, not by QcomQrtr. */
static void rg_our_qrtr(const uint8_t *p,uint32_t bytes)
{
 if(bytes<RG_HDR+4)return;
 const uint32_t type=rg_le32(p+4),size=rg_le32(p+20),dst_port=rg_le32(p+28);
 if(size>bytes-RG_HDR)return;
 rgw.packets_in++;
 if(type==RG_Q_HELLO){rgw.hello_sent=true;return;}
 if(type==RG_Q_NEW_LOOKUP){
  if(size<12)return;
  rgw.lookups++;
  /* The lookup's payload is {command, service, instance, node, port}, the last two zero when asking (Linux's
   * qmi_interface.c qmi_send_new_lookup). Only a lookup for a service we have is answered. */
  const uint32_t service=rg_le32(p+RG_HDR+4),instance=rg_le32(p+RG_HDR+8);
  rgw.last_lookup_service=service;rgw.last_lookup_instance=instance;
  if(service!=0x45u || (instance && instance!=0x301u) || opt_lookup_silent)return;
  if(opt_lookup_delay_ms){
   /* 2248b: the answer waits, so a request bound starts while the lookup's own bound is still running out. */
   rg_lookup_pending=1;rg_lookup_due_ms=rg_now_ms+opt_lookup_delay_ms;return;
  }
  const uint32_t w[4]={0x45u,0x301u,13u,RG_WLFW_PORT};
  rg_send_control(RG_Q_NEW_SERVER,w);
  if(opt_lookup_end_marker){const uint32_t zero[4]={0,0,0,0};rg_send_control(RG_Q_NEW_SERVER,zero);}
  return;
 }
 if(type==RG_Q_DATA){
  /* A packet from us is addressed to the SERVER's port (0x3001), not to our own: the port number in the header is the
   * destination's. Checking it is what catches a client that sends to the wrong place. */
  if(dst_port!=RG_WLFW_PORT){rgw.unexpected++;return;}
  if(size<7)return;
  rg_our_request(p+RG_HDR,size);
 }
}
/* The exchange, one tick at a time, from the same place the SMP2P tick runs so time behaves as on the phone. */
static void rg_tick(uint64_t ms)
{
 if(!glink_their_fifo || opt_glink_silent)return;
 if(!rg.acked || !rg.intented)return;
 rg_now_ms=(uint32_t)(now_us/1000);
 uint8_t m[8192];uint32_t bytes=0;
 while(rg_read_our(m,&bytes)){
  const uint32_t cmd=(uint32_t)m[0]|(uint32_t)m[1]<<8;
  if(cmd==RG_TX_DATA || cmd==RG_TX_DATA_CONT){
   const uint32_t id=rg_le32(m+4),len=rg_le32(m+8);
   if(bytes>=16 && len<=bytes-16){
    rg_our_qrtr(m+16,len);
    rgw.txdone_owed=true;rgw.txdone_id=id;
   }
  } else if(cmd==RG_RX_DONE_W_REUSE){rgw.intent_busy=false;
  } else if(cmd==RG_INTENT){
   const uint32_t pairs=(uint32_t)m[4]|(uint32_t)m[5]<<8;
   if(pairs){rgw.intent_size=rg_le32(m+8);rgw.intent_id=rg_le32(m+12);rgw.intent_seen=true;}
  }
 }
 if(rgw.txdone_owed){
  uint8_t d[8]={RG_RX_DONE_W_REUSE,0,0x41,0,0,0,0,0};
  for(unsigned i=0;i<4;i++)d[4+i]=(uint8_t)((rgw.txdone_id>>(8*i))&0xff);
  if(rg_send_raw(d,8))rgw.txdone_owed=false;
 }
 /* One queued packet per free intent, in order: the intent protocol allows exactly one outstanding packet. */
 if(!rgw.txdone_owed && rgw.out_count && !rgw.intent_busy){
  const unsigned slot=rgw.out_head;
  uint8_t whole[536];
  for(unsigned i=0;i<16;i++)whole[i]=0;
  whole[0]=RG_TX_DATA;whole[2]=0x41;whole[3]=0;                    /* the lcid the DSP opened */
  for(unsigned i=0;i<4;i++)whole[4+i]=(uint8_t)((rgw.intent_id>>(8*i))&0xff);
  for(unsigned i=0;i<4;i++)whole[8+i]=(uint8_t)((rgw.out_bytes[slot]>>(8*i))&0xff);
  memcpy(whole+16,rgw.out[slot],rgw.out_bytes[slot]);
  if(rg_send_raw(whole,16+rgw.out_bytes[slot])){
   rgw.out_head=(rgw.out_head+1)%8;rgw.out_count--;rgw.intent_busy=true;
  }
 }
 if(rg_lookup_pending && rg_now_ms>=rg_lookup_due_ms && !rgw.out_count){
  const uint32_t w[4]={0x45u,0x301u,13u,RG_WLFW_PORT};
  if(rg_send_control(RG_Q_NEW_SERVER,w))rg_lookup_pending=0;
 }
 while(rg_hold_count && rg_now_ms>=rg_hold[rg_hold_head].due_ms && !rgw.out_count){
  /* 2248b: a withheld reply, sent now that its time has come, oldest first. It is byte for byte the reply the request
   * asked for, from the announced port, so only its timing can distinguish it. */
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,rg_hold[rg_hold_head].bytes,rg_hold[rg_hold_head].n)){
   rg_hold_head=(rg_hold_head+1)%RG_HOLD_SLOTS;rg_hold_count--;
  } else break;
 }
 /* 2249 (W5): init_done once the board data is in. It goes out through the same one-packet-per-intent queue, so it
  * waits for a free intent exactly as a reply does. */
 if(!rgw.out_count)bdf_peer_maybe_init(rg_now_ms);
 /* 2250 (W6): the two indications the mode request is answered with, timed from the mode request's own arrival as the
  * reference port saw them (cal_done 6.7 s after the request, fw_ready 0.1 s after cal_done). The model uses much shorter
  * delays so a test does not need 7 s of fake time, but the order is the same, and opt_ready_before_cal can reverse it
  * because a firmware is free to and the host must still take both. A request_mem indication goes out first if asked for,
  * before the mode request is even answered: that is when a firmware which wanted memory would ask for it. */
 if(!rgw.out_count && mode_seen && opt_request_mem && !rbf.failed){
  static int mem_sent;
  if(!mem_sent){
   /* TLV 0x01: u8 count = 1, then one segment {u32 size, u32 type, u8 mem_cfg_len = 0}, built with this peer's own TLV
    * writer. The value is 1 + 4 + 4 + 1 = 10 bytes; the message is 7 + 3 + 10 = 20. */
   uint8_t p[7+3+10];
   p[0]=4;rg_put16(p+1,7);rg_put16(p+3,0x0035);
   uint8_t seg[10];seg[0]=1;rg_put32(seg+1,0x100000u);rg_put32(seg+5,3u);seg[9]=0;   /* size 1 MiB, type 3 = M3 */
   const uint32_t at=rg_tlv(p,7,0x01,seg,sizeof seg);
   rg_put16(p+5,at-7);
   if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,p,at))mem_sent=1;
  }
 }
 if(!rgw.out_count && mode_replied && !cal_sent && !opt_no_cal_done && rg_now_ms>=mode_at_ms+opt_cal_ms){
  /* A seven-byte indication with no TLV at all: ath11k's qmi_wlanfw_cold_boot_cal_done_ind_msg_v01_ei holds only the end
   * marker, exactly as fw_init_done's does, so the header's length is 0. */
  uint8_t r[7]={4,9,0,0x3e,0,0,0};
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,7)){cal_sent=1;if(opt_repeat_cal)opt_repeat_cal=2;}
 }
 /* A second cold_boot_cal_done, one tick later, for the repeated-indication control: the duplicate is a separate packet,
  * as a firmware that answered twice would send, not two messages glued into one datagram. */
 if(!rgw.out_count && opt_repeat_cal==2){opt_repeat_cal=3;}
 else if(!rgw.out_count && opt_repeat_cal==3){
  uint8_t r[7]={4,11,0,0x3e,0,0,0};
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,7))opt_repeat_cal=4;
 }
 if(!rgw.out_count && mode_replied && !opt_no_fw_ready && !ready_sent &&
    (opt_ready_before_cal ? rg_now_ms>=mode_at_ms+opt_cal_ms/2 : (cal_sent && rg_now_ms>=mode_at_ms+opt_cal_ms+opt_ready_ms_after_cal))){
  /* fw_ready, likewise seven bytes and no TLV. */
  uint8_t r[7]={4,10,0,0x21,0,0,0};
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,7))ready_sent=1;
 }
 /* 2251 (W7): what an idle post-mission firmware might do instead of waiting, and the two things the host must survive.
  *
  * opt_fatal_after_mission raises the SMP2P fatal bit two fake ticks after the mission request was answered. That is the
  * one failure mode a firmware with no host data path can produce that the host must notice: the stage's main loop sets
  * w.fatal on it, the run ends with EIO, and the verified stop still has to happen. 2 ticks is enough for the mission
  * reply to have been taken, so this is genuinely "after mission mode", not a race with it.
  *
  * opt_mission_indication volunteers an fw_ready-shaped indication once mission mode is taken. W7 counts those separately,
  * because its question is what an idle firmware does: silence is the expected answer, and an unsolicited indication is
  * evidence about the firmware rather than a failure. */
 if(mission_taken && opt_fatal_after_mission && !fatal_raised && rg_now_ms>=mission_at_ms+2){
  qcom_smp2p_update(&remote,0,1,1);fatal_raised=1;
 }
 if(mission_taken && opt_mission_indication && !rgw.out_count && !mission_indication_sent){
  uint8_t r[7]={4,12,0,0x21,0,0,0};
  if(rg_send_qrtr(RG_Q_DATA,RG_WLFW_PORT,RG_OUR_PORT,r,7))mission_indication_sent=1;
 }
 if(rgw.hello_sent && !rgw.announced && ms>=opt_ready_ms+400){
  const uint32_t hello[4]={0,0,0,0};
  rg_send_control(RG_Q_HELLO,hello);
  if(!opt_no_services){
   /* The five ast222 saw, in its order. WLFW comes from its own port so a reply's sender is distinguishable. */
   static const uint32_t svc[5]={0x42,0x45,0x2b,0x0f,0x33},inst[5]={0x6f01,0x301,0x1902,0x8001,0x301};
   for(unsigned k=0;k<5;k++){
    if(opt_wlfw_quiet && svc[k]==0x45u)continue;      /* only the lookup finds it: that is L1's question */
    const uint32_t w[4]={svc[k],inst[k],13u,svc[k]==0x45u?RG_WLFW_PORT:(0x3100u+k)};
    rg_send_control(RG_Q_NEW_SERVER,w);
   }
  }
  rgw.announced=true;
 }
}


static void remote_start(void){
 QcomSmem sm=host_smem();QcomSmemPartition part;assert(!qcom_smem_partition(&sm,0,13,&part));
 /* 2247 (W3 audit note 6): the phone already has item 478 (W1 measured 32 bytes) and 479, so the real boot takes the
  * descriptor-read branch and the QcomSmemExists path for our FIFO. The 2246 model had neither, so the branch the phone
  * takes had never run. Both are created here, with 478's words left as a boot stage would: zeros. */
 if(!opt_no_glink_desc){
  int mk=qcom_smem_alloc(&sm,&part,478,32);assert(mk==QcomSmemOk || mk==QcomSmemExists);
  uint32_t doff2=0,dbytes2=0;assert(!qcom_smem_find(&sm,&part,478,&doff2,&dbytes2));
  for(unsigned i=0;i<8;i++){uint32_t z=0;memcpy((void *)((uint8_t *)regions[R_SMEM].va+doff2+4*i),&z,4);}
  glink_desc_off=doff2;
 }
 int made=qcom_smem_alloc(&sm,&part,WPSS_ITEM,QCOM_SMP2P_ITEM_BYTES);assert(made==QcomSmemOk || made==QcomSmemExists);
 uint32_t off,bytes;assert(!qcom_smem_find(&sm,&part,WPSS_ITEM,&off,&bytes));
 memset(&remote,0,sizeof(remote));qcom_smp2p_init_out(&remote,(volatile uint32_t *)(regions[R_SMEM].va+off),13,0);
 assert(qcom_smp2p_add_entry(&remote,"slave-kernel")==0);
 /* 2246 (W3): the DSP's GLINK FIFO, item 480, which W1 found absent and the WPSS makes when it runs. */
 if(!opt_no_glink_fifo){
  int f=qcom_smem_alloc(&sm,&part,480,0x4000);assert(f==QcomSmemOk || f==QcomSmemExists);
  uint32_t foff=0,fbytes=0;assert(!qcom_smem_find(&sm,&part,480,&foff,&fbytes));
  uint32_t head=0,tail=0;memcpy((void *)((uint8_t *)regions[R_SMEM].va+foff),&head,4);memcpy((void *)((uint8_t *)regions[R_SMEM].va+foff+4),&tail,4);
  glink_their_fifo=foff;glink_their_bytes=fbytes;
 }
 remote_running=true;remote_made=true;remote_started_us=now_us;
}
static void remote_tick(void){
 if(!remote_running)return;
 uint64_t ms=(now_us-remote_started_us)/1000;
 if(!opt_no_ready && ms>=opt_ready_ms)qcom_smp2p_update(&remote,0,2,2);
 if(!opt_no_ready && !opt_no_handover && ms>=opt_ready_ms+100)qcom_smp2p_update(&remote,0,4,4);
 if(opt_fatal_ms>=0 && ms>=(uint64_t)opt_fatal_ms)qcom_smp2p_update(&remote,0,1,1);
 /* Our item: find "master-kernel" bit 0. */
 QcomSmem sm=host_smem();QcomSmemPartition part;uint32_t off,bytes;
 if(qcom_smem_partition(&sm,0,13,&part) || qcom_smem_find(&sm,&part,HOST_ITEM,&off,&bytes))return;
 QcomSmp2p view={0};view.in=(volatile uint32_t *)(regions[R_SMEM].va+off);
 if(qcom_smp2p_check_in(view.in,13,0))return;
 int i=qcom_smp2p_find_in(&view,"master-kernel");
 if(i>=0 && (qcom_smp2p_in_value(&view,i)&1) && !opt_no_ack){qcom_smp2p_update(&remote,0,8,8);remote_acked=true;}
 remote_glink_tick(ms);
}

/* SMEM with one empty APPS-WPSS partition, and a cmd-db with cx.lvl, mx.lvl and xo.lvl. */
static void put32(int r,unsigned off,uint32_t v){memcpy(at(r,off),&v,4);}
static void spmi_decoy(void){put32(R_SPMI_CORE,0x2000+4*12,0x0bcu<<8);put32(R_SPMI_CNFG,4*12,3);}
static void world_init(void){
 regions_init();
 unsigned toc=0x200000u-0x1000u;
 put32(R_SMEM,toc,0x434f5424);put32(R_SMEM,toc+4,1);put32(R_SMEM,toc+8,1);
 /* 2246: the real APPS-WPSS partition is 0x40000 bytes (W1: partition 0-13 at 0x83000, 0x40000), not the 0x8000 this
  * model used to fake. W3's GLINK FIFO plus the two SMP2P items need more than 0x8000, so the model has to match the phone
  * or it would refuse a start the phone would accept. */
 put32(R_SMEM,toc+32,0x1000);put32(R_SMEM,toc+36,0x40000);put32(R_SMEM,toc+44,13u<<16);put32(R_SMEM,toc+48,8);
 put32(R_SMEM,0x1000,0x54525024);put32(R_SMEM,0x1004,13u<<16);put32(R_SMEM,0x1008,0x40000);put32(R_SMEM,0x100c,0x20);put32(R_SMEM,0x1010,0x40000);
 uint8_t *b=at(R_DB,0);put32(R_DB,0,1);put32(R_DB,4,0x0c0330db);
 b[8]=1;b[12]=72;b[14]=3;
 static const char *names[3]={"cx.lvl","mx.lvl","xo.lvl"};
 static const uint16_t lv[3][8]={{0,16,48,64,128,192,256,384},{0,48,64,128,192,256,384,416},{0,32,80,128}};
 for(unsigned i=0;i<3;i++){
  unsigned e=144+24*i;memcpy(b+e,names[i],strlen(names[i]));
  uint32_t address=0x30000+0x10*i;memcpy(b+e+16,&address,4);
  uint16_t len=i==2?8:16,aux=(uint16_t)(16*i);memcpy(b+e+20,&len,2);memcpy(b+e+22,&aux,2);
  memcpy(b+216+16*i,lv[i],len);
 }
 /* RSC: DRV version 3, three idle TCSs. */
 put32(R_RSC,0,3u<<16);for(unsigned i=0;i<3;i++)put32(R_RSC,0xd00+i*0x2a0+0x28,1);
 put32(R_SPMI_CORE,0,0x70020000u);put32(R_SPMI_CORE,4,0x180u);
 put32(R_SPMI_CORE,0x2000+4*5,0x071u<<8);put32(R_SPMI_CORE,0x2000+4*GPIO5_APID,0x0bcu<<8);put32(R_SPMI_CNFG,4*GPIO5_APID,0);
 /* GPIO5 as W1 (ast200) read it; other registers carry distinct filler so stale reads would show. */
 for(unsigned i=0;i<256;i++)gpio5_regs[i]=(uint8_t)(0xa0+i);
 static const uint8_t ast200[10][2]={{4,0x10},{5,0x12},{8,0x80},{0x40,0},{0x41,0},{0x42,4},{0x43,0},{0x44,0},{0x45,1},{0x46,0x80}};
 for(unsigned i=0;i<10;i++)gpio5_regs[ast200[i][0]]=ast200[i][1];
 spmi_fail=spmi_hang=0;spmi_cmds=0;pmic_nwrites=0;chan_fail_at=stuck_reg=-1;
 rsc_nwrites=ipcc_nrings=pas_ncalls=qmp_nsent=0;memset(pas_status,0,sizeof(pas_status));
 shutdown_einval=auth_interrupt=0;remote_running=remote_acked=remote_made=false;
 opt_no_ready=opt_no_handover=opt_no_ack=0;opt_fatal_ms=-1;fail_map=-1;fail_fw=0;qmp_fail_open=qmp_fail_off=0;opt_ready_ms=300;
 opt_no_glink_fifo=opt_glink_silent=opt_no_open=opt_no_hello=opt_no_intent=opt_no_glink_desc=0;opt_services=0;glink_their_fifo=glink_their_bytes=glink_their_opens=0;
 memset(&rg,0,sizeof(rg));memset(&remote,0,sizeof(remote));
 opt_lookup_silent=opt_lookup_end_marker=opt_wlfw_quiet=opt_no_services=0;opt_intent_size=0x4400u;
 opt_refuse=opt_silent=opt_short_reply=opt_no_board=0;opt_board=0;memset(&rgw,0,sizeof(rgw));
 opt_hold_reply=opt_repeat_reply=opt_wrong_id=opt_wrong_id_for=0;opt_hold_ms=6000;
 memset(rg_hold,0,sizeof(rg_hold));rg_hold_head=rg_hold_count=0;rg_now_ms=0;
 opt_lookup_delay_ms=0;rg_lookup_pending=0;rg_lookup_due_ms=0; meta_allocs=meta_frees=syncs=kallocs=0;hlog_len=0;hlog[0]=0;now_us=0;
 /* 2249 (W5): the board data peer's own state, reset with everything else. */
 opt_no_init_done=0;opt_early_init_done=0;opt_refuse_bdf_at=0xffffffffu;bdf_pieces_seen=0;memset(&rbf,0,sizeof(rbf));
 /* 2250 (W6): the mode stage's peer state. */
 opt_no_cal_done=opt_no_fw_ready=opt_ready_before_cal=opt_repeat_cal=opt_request_mem=opt_refuse_mode=0;
 opt_mode_bytes=0;opt_mode_value=0xffffffffu;opt_mode_requests=0;opt_cal_ms=200;opt_ready_ms_after_cal=50;
 mode_replied_ms=mode_at_ms=0;mode_seen=mode_replied=cal_sent=ready_sent=0;
 /* 2251 (W7): the mission half's peer state, reset with everything else so one scenario cannot leak into the next. */
 opt_refuse_ini=opt_refuse_cfg=opt_refuse_mission=opt_refuse_off=opt_no_mission_reply=opt_fatal_after_mission=0;
 opt_mission_indication=0;ini_seen=ini_bytes=ini_value=cfg_seen=cfg_bytes=cfg_version_ok=0;
 cfg_pipes=cfg_svcs=cfg_shadow=0;cfg_pipes_ok=cfg_svcs_ok=cfg_shadow_ok=0;
 mission_at_ms=mission_requests=mode_off_requests=mode_off_at_ms=0;mission_replied=mission_taken=off_replied=off_taken=0;
 fatal_raised=0;cfg_accepted=0;mission_indication_sent=0;
 /* 2252 (W8 survey): apps_smmu as ast260 read it. Banks 0 to 4 are ABL's (CBAR 0x1f000, CBA2R 1, SCTLR 0xe0, FSR 0x400);
  * banks 5 to 71 carry UNKNOWN reset values, here a pattern in which every fifth bank has CFRE/M set so the free count is
  * not just "all of them". */
 smmu_claimed=smmu_claim_other=0;smmu_table_reads=0;opt_usf_in_mission=0;
 /* 2253 (W9): the lease model's own state, reset with everything else. A run is one lease: one install, one release. */
 smmu_w9_state=W9S_IDLE;smmu_w9_bank=smmu_w9_slot=smmu_w9_vmid=-1;smmu_write_bad=smmu_writes=w9_installs=w9_releases=0;
 w9_seen_cbar=w9_seen_on=w9_seen_ttbr=w9_seen_fault=w9_seen_off=false;smmu_w9_ttbr=0;
 smmu_w9_smr_old=smmu_w9_s2cr_old=smmu_w9_cbar_old=smmu_w9_cba2r_old=0;
 smmu_fault_at_teardown=smmu_fault_raised=0;
 put32(R_SMMU,0,0x2a0406u);put32(R_SMMU,0x20,0x4c017e72u);put32(R_SMMU,0x24,0x60000048u);put32(R_SMMU,0x28,0x5111u);
 static const uint32_t abl_smr[5]={0x80000060u,0x80000540u,0x80021c00u,0x80000040u,0x80000480u};
 for(unsigned i=0;i<5;i++){put32(R_SMMU,0x800+4*i,abl_smr[i]);put32(R_SMMU,0xc00+4*i,i);}
 for(unsigned c=0;c<SMMU_NCB;c++){
  unsigned b=0x80000+0x1000*c;
  if(c<5){put32(R_SMMU,0x1000+4*c,0x1f000u);put32(R_SMMU,0x1800+4*c,1);put32(R_SMMU,b,0xe0);put32(R_SMMU,b+0x58,0x400);}
  else {put32(R_SMMU,b,c%5==0?0x3ec98521u:0x3ec9850cu);put32(R_SMMU,b+0x20,0x9191f7efu^c);put32(R_SMMU,b+0x24,0x73bf763bu);}
 }
}

/* Kernel services sweet_wpss.c uses. */
static const char *host_args="";static int host_deadman=-1;
static const char *PE_boot_args(void){return host_args;}
static int PE_parse_boot_argn(const char *n,void *out,int size){assert(!strcmp(n,"sweetdeadman") && size==4);if(host_deadman<0)return 0;*(int *)out=host_deadman;return 1;}
static int threads;
static kern_return_t kernel_thread_start(void (*f)(void *,wait_result_t),void *arg,thread_t *t){threads++;*t=1;f(arg,0);return KERN_SUCCESS;}
static void thread_deallocate(thread_t t){(void)t;}
static thread_t current_thread(void){return 1;}
static kern_return_t thread_terminate(thread_t t){(void)t;return 0;}
/* sweet_spmi.c is the SM7150/SDM845 arbiter v5 driver and has never run on this phone (W1 audit, blocking defect 1). */
static int sweet_spmi_init(void){fprintf(stderr,"FAIL: v5 SPMI driver (sweet_spmi.c) called on asteroids\n");abort();}
static int sweet_spmi_read(unsigned sid,unsigned addr,uint8_t *out){(void)sid;(void)addr;(void)out;fprintf(stderr,"FAIL: v5 SPMI driver (sweet_spmi.c) called on asteroids\n");abort();}
