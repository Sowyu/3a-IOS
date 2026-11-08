#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#define META_MAX 65536u
#define VM_WIMG_WCOMB 1
struct wl_fat {int unused;};
struct adsp_image {const char *stem;unsigned pas;uint64_t base,bytes,meta_pa;uint8_t *mdt;uint32_t mdt_bytes;uintptr_t mapping,metadata;bool owed,started;};
static struct adsp_image *active;
static uint8_t metadata[META_MAX],firmware[4096];
static unsigned maps,pas_calls,fail_map,fail_pas,fail_file;
static uint32_t le32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint16_t le16(const uint8_t *p){uint16_t v;memcpy(&v,p,2);return v;}
typedef uintptr_t vm_offset_t;typedef size_t vm_size_t;
#define META_PA 0x8123400000ull
static unsigned synced;
/* 2290: metadata comes from contiguous kernel memory, never a fixed physical page. */
static int m_contig(vm_offset_t *va,uint64_t *pa,vm_size_t len){assert(len==META_MAX);if(++maps==fail_map){*va=0;return ENOMEM;}*va=(uintptr_t)metadata;*pa=META_PA;return 0;}
static void sweet_dma_sync_va(const void *va,size_t len){assert(va==metadata && len==META_MAX);synced++;}
static uintptr_t ml_io_map_unmappable(uintptr_t addr,size_t len,unsigned flags){assert(flags==VM_WIMG_WCOMB);if(++maps==fail_map)return 0;assert(addr==active->base && len==4096);return (uintptr_t)firmware;}
static uint32_t qcom_scm_args(unsigned a,unsigned b,unsigned c,unsigned d){(void)b;(void)c;(void)d;return a;}
static int m_pas(unsigned pas,unsigned cmd,unsigned info,uint64_t b,uint64_t c){(void)info;(void)c;assert(pas==1 && active->owed);if(cmd==1)assert(b==META_PA && synced==1 && !memcmp(metadata,active->mdt,active->mdt_bytes));assert(cmd==1 || cmd==2 || cmd==5);return ++pas_calls==fail_pas ? -EIO:0;}
static int fat_open(struct wl_fat *f,const char *path,uint32_t *cl,uint32_t *n){(void)f;assert(!strcmp(path,"image/adsp.b01"));*cl=2;*n=8;return fail_file?EIO:0;}
static int fat_read_chain(struct wl_fat *f,uint32_t cl,uint64_t off,void *p,uint64_t len){(void)f;assert(cl==2 && off==0 && len==8);memset(p,0x5a,8);return 0;}
