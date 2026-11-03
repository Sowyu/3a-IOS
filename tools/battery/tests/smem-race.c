#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static uint32_t memory[0x4000/4];
static unsigned size_reads;
static void before_read(uint32_t offset){
 if(offset==0x1024 && ++size_reads==2)memory[offset/4]=0xfffffff8;
}
#include "smem-race-source.c"
int main(void){
 QcomSmem m={(volatile uint8_t *)memory,sizeof(memory)};
 QcomSmemPartition p={0x1000,0x1000,8,0,2};
 memory[0x1000/4]=0x54525024;memory[0x1004/4]=2u<<16;memory[0x1008/4]=0x1000;
 memory[0x100c/4]=0x188;memory[0x1010/4]=0x1000;
 memory[0x1020/4]=0xa5a5u|(443u<<16);memory[0x1024/4]=344;memory[0x1028/4]=4;
 uint32_t offset,bytes;assert(!qcom_smem_find(&m,&p,443,&offset,&bytes));
 assert(offset==0x1030 && bytes==340 && size_reads==1);
 puts("PASS: uncached extent comes from one validated read despite a second-read mutation");
}
