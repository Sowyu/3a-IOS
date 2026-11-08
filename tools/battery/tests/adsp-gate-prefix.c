/* Host model for bsd/dev/sweet_adsp.c: the gate, the worker and the shutdown handshake. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include "ModemGate.h"
#define ARM64_BOARD_CONFIG_SWEET 1
typedef void *thread_t;typedef int wait_result_t;typedef int kern_return_t;
#define THREAD_NULL NULL
#define KERN_SUCCESS 0
typedef void (*thread_continue_t)(void *,wait_result_t);
static const char *boot_args="";
static const char *PE_boot_args(void){return boot_args;}
static int PE_parse_boot_argn(const char *name,void *out,int size){
 size_t n=strlen(name);const char *p=boot_args;
 while((p=strstr(p,name))){if((p==boot_args || p[-1]==' ') && p[n]=='='){*(int *)out=atoi(p+n+1);(void)size;return 1;}p+=n;}
 return 0;
}
static thread_continue_t started_fn;static void *started_arg;static unsigned starts;
static kern_return_t kernel_thread_start(thread_continue_t fn,void *arg,thread_t *t){started_fn=fn;started_arg=arg;starts++;*t=(thread_t)1;return KERN_SUCCESS;}
static void thread_deallocate(thread_t t){(void)t;}
static thread_t current_thread(void){return (thread_t)1;}
static kern_return_t thread_terminate(thread_t t){(void)t;return 0;}
static void sweet_log_sync(void){}
static volatile bool adsp_stop_requested;
static unsigned sleeps,runs,finish_after;static bool run_glink,run_reads,run_daily;
static bool *active_flag;
/* m_sleep: 10 ms steps in sweet_adsp_shutdown; the worker's 30 s start delay is one call. */
static void m_sleep(unsigned ms){sleeps++;(void)ms;if(finish_after && sleeps>=finish_after && active_flag)*active_flag=false;}
static bool run_meter;
static int adsp_run(bool glink,bool reads,bool daily,bool meter){runs++;run_glink=glink;run_reads=reads;run_daily=daily;run_meter=meter;return 0;}
