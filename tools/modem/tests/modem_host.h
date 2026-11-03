#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define Z_WAITOK 0
#define Z_ZERO 0
#define KERN_SUCCESS 0
typedef int wait_result_t;
typedef int thread_t;
static int boot_level, threads, reads;
static uint8_t disk[4200*512];
static int sweet_ufs_read_part(const char *p, uint64_t o, void *b, uint64_t n) {
 assert(!strcmp(p,"modem_a")); assert(o<=sizeof(disk) && n<=sizeof(disk)-o);
 reads++; memcpy(b,disk+o,n); return 0;
}
static int sweet_ufs_part_size(const char *p,uint8_t *l,uint64_t *n) {
 assert(!strcmp(p,"modem_a")); *l=4; *n=sizeof(disk); return 0;
}
static void *kalloc_data(size_t n,int flags) {(void)flags;return calloc(1,n);}
/* Host checks retain allocations until process exit, following the Trash rule. */
static void kfree_data(void *p,size_t n) {(void)p;(void)n;}
static int current_thread(void) {return 0;}
static void thread_terminate(int t) {(void)t;}
static void thread_deallocate(int t) {(void)t;}
static int PE_parse_boot_argn(const char *name,void *v,int n) {
 assert(!strcmp(name,"sweetmodem") && n==sizeof(int)); *(int *)v=boot_level;return boot_level!=-99;
}
static int kernel_thread_start(void (*fn)(void *,int),void *a,thread_t *t) {
 threads++; *t=1;fn(a,0);return 0;
}
