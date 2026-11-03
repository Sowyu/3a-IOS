#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
typedef int kern_return_t;
typedef int thread_t;
typedef int wait_result_t;
#define VM_WIMG_IO 7
#define NSEC_PER_MSEC 1000000
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
#define THREAD_NULL 0
#define KERN_SUCCESS 0
static char *args;
static unsigned starts,maps,unmaps;
static int failmap;
static uint32_t smem[0x200000/4],db[0x20000/4];
static const char *PE_boot_args(void){return args;}
static void clock_interval_to_deadline(unsigned a,unsigned b,uint64_t *d){*d=(uint64_t)a*b;}
static void assert_wait_deadline(void *a,int b,uint64_t c){(void)a;(void)b;(void)c;}
static void thread_block(int a){(void)a;}
static thread_t current_thread(void){return 1;}
static void thread_deallocate(thread_t t){(void)t;}
static kern_return_t kernel_thread_start(void (*f)(void *,wait_result_t),void *arg,thread_t *t){starts++;*t=1;f(arg,0);return 0;}
