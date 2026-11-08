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
/* 2297: counts kicks to SweetBattery. Not static, so the pre-2297 control compiles with it unused. */
unsigned kicks;
void sweet_battery_kick(void){kicks++;}
#include "snapshot-source.inc"
int main(void){
 BatterySnapshot s={.percent=-1};assert(!sweet_adsp_battery_snapshot(&s) && s.percent==-1);
 /* The eight raw cycles of ast192, in order: status present capacity uV uA cC online,
  * then what iOS must get: percent mV mA (positive while charging) external charging full. */
 static const struct {int32_t raw[7];int percent,mv,ma,external,charging,full;} ast192[]={
  {{2,1,9994,4371000,-1260712,3000,1},100,4371,-1260,1,0,0},
  {{1,1,9994,4471040,435801,3000,1},100,4471,435,1,1,0},
  {{1,1,10000,4502181,910056,2900,1},100,4502,910,1,1,0},
  {{1,1,10000,4504905,874350,3000,1},100,4504,874,1,1,0},
  {{4,1,10000,4460724,-1831,3000,1},100,4460,-1,1,0,1},
  {{4,1,10000,4455469,-3662,3000,1},100,4455,-3,1,0,1},
  {{4,1,10000,4454496,-2746,3000,1},100,4454,-2,1,0,1},
  {{4,1,10000,4453523,-3662,3000,1},100,4453,-3,1,0,1}};
 for(unsigned i=0;i<sizeof(ast192)/sizeof(ast192[0]);i++){
  BatteryRead r={.cursor=BatteryFields};for(unsigned j=0;j<7;j++)r.value[j]=ast192[i].raw[j];
  battery_snapshot_publish(&r,1000);clock_ns=1000*NSEC_PER_MSEC;
  assert(sweet_adsp_battery_snapshot(&s) && s.percent==ast192[i].percent && s.millivolts==ast192[i].mv && s.milliamps==ast192[i].ma);
  assert(s.external==ast192[i].external && s.charging==ast192[i].charging && s.full==ast192[i].full && s.temperature==ast192[i].raw[5]);
 }
 BatteryRead b={.value={1,1,7340,4200000,450000,2875,1},.cursor=BatteryFields};
 battery_snapshot_publish(&b,1000);clock_ns=1000*NSEC_PER_MSEC;
 assert(sweet_adsp_battery_snapshot(&s) && s.percent==73 && s.millivolts==4200 && s.milliamps==450 && s.temperature==2875 && s.external && s.charging && !s.full);
 clock_ns=46000*NSEC_PER_MSEC;assert(sweet_adsp_battery_snapshot(&s));
 clock_ns=46001*NSEC_PER_MSEC;assert(!sweet_adsp_battery_snapshot(&s));
 clock_ns=999*NSEC_PER_MSEC;assert(!sweet_adsp_battery_snapshot(&s));
 clock_ns=1000*NSEC_PER_MSEC;b.value[0]=2;b.value[4]=-750000;b.value[6]=0;battery_snapshot_publish(&b,1000);
 assert(sweet_adsp_battery_snapshot(&s) && !s.external && !s.charging && s.milliamps==-750);
 battery_snapshot_clear();assert(!sweet_adsp_battery_snapshot(&s));
 b.value[2]=10001;battery_snapshot_publish(&b,1000);assert(!sweet_adsp_battery_snapshot(&s));
 b.value[2]=7340;b.cursor=3;battery_snapshot_publish(&b,1000);assert(!sweet_adsp_battery_snapshot(&s));
 /* 2297: a kick when what iOS shows changes, or after no valid sample; not for voltage or current alone. */
 battery_snapshot_clear();kicks=0;BatteryRead k={.value={2,1,8040,4100000,-500000,2900,1},.cursor=BatteryFields};
 battery_snapshot_publish(&k,1000);assert(kicks==1 && "kick on the first sample");
 k.value[3]=4090000;k.value[4]=-650000;k.value[5]=2950;battery_snapshot_publish(&k,1005);assert(kicks==1 && "no kick for voltage, current or temperature");
 k.value[0]=1;battery_snapshot_publish(&k,1010);assert(kicks==2 && "kick when charging starts");
 k.value[6]=0;k.value[0]=2;battery_snapshot_publish(&k,1015);assert(kicks==3 && "kick on unplug");
 k.value[2]=7940;battery_snapshot_publish(&k,1020);assert(kicks==4 && "kick when the percent changes");
 k.value[0]=4;k.value[6]=1;battery_snapshot_publish(&k,1025);assert(kicks==5);
 k.value[1]=0;battery_snapshot_publish(&k,1030);assert(kicks==5 && "an invalid sample clears, no kick");
 assert(!locked);puts("PASS: complete atomic snapshot, ast192 real cycles and sign (positive charging), 45s expiry, rollback and explicit invalidation");
}
