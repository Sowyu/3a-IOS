#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define NSEC_PER_MSEC 1000000ull
#define LCK_GRP_DECLARE(name,label) int name __attribute__((unused))
#define LCK_MTX_DECLARE(name,group) int name
static uint64_t clock_ns;
static unsigned locked;
static void lck_mtx_lock(int *p){(void)p;assert(!locked);locked=1;}
static void lck_mtx_unlock(int *p){(void)p;assert(locked);locked=0;}
static uint64_t mach_absolute_time(void){return clock_ns;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
#include "snapshot-source.inc"
int main(void){
 BatterySnapshot s={.percent=-1};assert(!sweet_adsp_battery_snapshot(&s) && s.percent==-1);
 BatteryRead b={.value={1,1,7340,4200000,-450000,2875,1},.cursor=BatteryFields};
 battery_snapshot_publish(&b,1000);clock_ns=1000*NSEC_PER_MSEC;
 assert(sweet_adsp_battery_snapshot(&s) && s.percent==73 && s.millivolts==4200 && s.milliamps==450 && s.temperature==2875 && s.external && s.charging && !s.full);
 clock_ns=46000*NSEC_PER_MSEC;assert(sweet_adsp_battery_snapshot(&s));
 clock_ns=46001*NSEC_PER_MSEC;assert(!sweet_adsp_battery_snapshot(&s));
 clock_ns=999*NSEC_PER_MSEC;assert(!sweet_adsp_battery_snapshot(&s));
 clock_ns=1000*NSEC_PER_MSEC;b.value[0]=2;b.value[4]=750000;b.value[6]=0;battery_snapshot_publish(&b,1000);
 assert(sweet_adsp_battery_snapshot(&s) && !s.external && !s.charging && s.milliamps==-750);
 battery_snapshot_clear();assert(!sweet_adsp_battery_snapshot(&s));
 b.value[2]=10001;battery_snapshot_publish(&b,1000);assert(!sweet_adsp_battery_snapshot(&s));
 b.value[2]=7340;b.cursor=3;battery_snapshot_publish(&b,1000);assert(!sweet_adsp_battery_snapshot(&s));
 assert(!locked);puts("PASS: complete atomic snapshot, unit/sign mapping, 45s expiry, rollback and explicit invalidation");
}
