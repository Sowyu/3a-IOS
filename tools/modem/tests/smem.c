#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "QcomSmem.h"
static uint32_t memory[0x4000/4];
static void w(unsigned at,uint32_t value){memory[at/4]=value;}
int main(void){
 QcomSmem s={(volatile uint8_t *)memory,sizeof(memory)};
 w(0x3000,0x434f5424);w(0x3004,1);w(0x3008,1);
 w(0x3020,0x1000);w(0x3024,0x1000);w(0x302c,1u<<16);w(0x3030,8);
 w(0x1000,0x54525024);w(0x1004,1u<<16);w(0x1008,0x1000);w(0x100c,32);w(0x1010,0x1000);
 QcomSmemPartition p;assert(!qcom_smem_partition(&s,0,1,&p));
 uint32_t at,n;assert(qcom_smem_find(&s,&p,428,&at,&n)==QcomSmemMissing);
 assert(!qcom_smem_alloc(&s,&p,428,340));assert(!qcom_smem_find(&s,&p,428,&at,&n));assert(at==0x1030 && n==340);
 assert(qcom_smem_alloc(&s,&p,428,340)==QcomSmemExists);
 uint32_t saved=memory[0x100c/4];w(0x100c,0xfffffff8);assert(qcom_smem_find(&s,&p,428,&at,&n)==QcomSmemCorrupt);w(0x100c,saved);
 w(0x1024,0xfffffff8);assert(qcom_smem_find(&s,&p,428,&at,&n)==QcomSmemCorrupt);w(0x1024,344);
 w(0x1028,345);assert(qcom_smem_find(&s,&p,428,&at,&n)==QcomSmemCorrupt);w(0x1028,4);
 QcomSmemPartition bad=p;bad.offset=0xfffffff8;assert(qcom_smem_find(&s,&bad,428,&at,&n)==QcomSmemCorrupt);
 w(0x1010,0xe00);w(0x1ff0,0xa5a5u|(435u<<16));w(0x1ff4,0x1000);
 assert(qcom_smem_find(&s,&p,435,&at,&n)==QcomSmemCorrupt);
 assert(qcom_smem_alloc(&s,&p,429,8)==QcomSmemCorrupt);
 w(0x1ff4,0x1f0);assert(!qcom_smem_find(&s,&p,435,&at,&n));assert(at==0x1e00 && n==0x1f0);
 assert(qcom_smem_alloc(&s,&p,429,0xfffffff8)==QcomSmemNoSpace);
 uint32_t before=memory[0];qcom_smem_write32(&s,UINT32_MAX,123);assert(memory[0]==before);
 assert(qcom_smem_read32(&s,UINT32_MAX)==0);
 puts("PASS: SMEM allocations, revalidated extents, corrupt cached/uncached entries, write bounds");
}
