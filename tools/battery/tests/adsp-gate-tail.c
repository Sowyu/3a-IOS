static void boot(const char *args){boot_args=args;starts=runs=sleeps=finish_after=0;started_fn=NULL;adsp_stop_requested=false;adsp_worker_active=false;active_flag=&adsp_worker_active;sweet_adsp_start();}
int main(void){
 /* Off unless a gate is set. */
 boot("");assert(!starts);boot("sweetbatt=0 sweetfbarm=1");assert(!starts);boot("sweetbatt sweetfbarm=1");assert(!starts);
 /* Daily preset: needs sweetfbarm, takes no selftest or deadman, turns on GLINK and reads. */
 boot("sweetbatt=1 sweetfbarm=1");assert(starts==1 && started_arg);started_fn(started_arg,0);assert(runs==1 && run_glink && run_reads && run_daily && !adsp_worker_active);
 boot("sweetbatt=1");assert(!starts);
 boot("sweetbatt=1 sweetfbarm=1 sweetbatt=1");assert(!starts);
 static const char *refused[]={"sweetbadsp=1","sweetbglink=1","sweetbread=1","sweetbpub=1","sweetbprobe=1","sweetmpss=1"};
 for(unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);i++){char line[128];snprintf(line,sizeof(line),"sweetbatt=1 sweetfbarm=1 %s",refused[i]);boot(line);assert(!starts);}
 /* The soak line adds the deadman, selftest and USB gadget; all allowed. */
 boot("sweetbatt=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=10800 sweetusb=4");assert(starts==1);started_fn(started_arg,0);assert(run_daily);
 /* Test gates keep their exact B2/B3 rules. */
 boot("sweetbadsp=1 sweetbglink=1 sweetbread=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=600");assert(starts==1 && !started_arg);started_fn(started_arg,0);assert(run_glink && run_reads && !run_daily);
 boot("sweetbadsp=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=900");assert(!starts);
 boot("sweetbadsp=1 sweetbread=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=600");assert(!starts);
 /* Shutdown before the worker's start delay ends: nothing starts and nothing waits. */
 boot("sweetbatt=1 sweetfbarm=1");sweet_adsp_shutdown();assert(sleeps==0);started_fn(started_arg,0);assert(!runs && !adsp_worker_active);
 /* Shutdown while the worker runs: wait in 10 ms steps until it finishes. */
 boot("sweetbatt=1 sweetfbarm=1");adsp_worker_active=true;finish_after=40;sweet_adsp_shutdown();assert(sleeps==40 && !adsp_worker_active && adsp_stop_requested);
 /* A worker that never finishes: the wait is bounded at 15 s. */
 boot("sweetbatt=1 sweetfbarm=1");adsp_worker_active=true;active_flag=NULL;sweet_adsp_shutdown();assert(sleeps==1500 && adsp_worker_active);
 /* No gate, no worker: shutdown returns at once. */
 boot("");sweet_adsp_shutdown();assert(!sleeps);
 /* 2292: sweetbmeter passes through; adsp_run ignores it outside daily mode. */
 boot("sweetbatt=1 sweetfbarm=1 sweetbmeter=1");started_fn(started_arg,0);assert(run_daily && run_meter);
 boot("sweetbatt=1 sweetfbarm=1");started_fn(started_arg,0);assert(!run_meter);
 puts("PASS: sweetbatt gate and refusals, test gates unchanged, shutdown stops a running worker within 15 s and prevents a late start");
}
