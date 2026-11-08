/* 2298: the frame walk the sampler's IRQ handler runs, extracted from osfmk/kern/processor.c. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cputop-walk.inc"
int main(void){
 static uint64_t stack[64];uint64_t lo=(uint64_t)stack,hi=lo+sizeof(stack),out[8];
 /* A chain of four records: [fp, lr] at word 4, 10, 20, 30, then a zero fp. */
 unsigned at[]={4,10,20,30};
 for(unsigned i=0;i<4;i++){stack[at[i]]=i<3?lo+8*at[i+1]:0;stack[at[i]+1]=0x1000+i;}
 assert(sct_walk(lo+8*4,lo,hi,out,8)==4 && out[0]==0x1000 && out[3]==0x1003);
 assert(sct_walk(lo+8*4,lo,hi,out,2)==2);                        /* at most max */
 stack[at[1]]=lo+8*at[0];assert(sct_walk(lo+8*4,lo,hi,out,8)==2); /* a backward link stops the walk */
 stack[at[1]]=lo+8*at[2];
 stack[at[2]]=hi+64;assert(sct_walk(lo+8*4,lo,hi,out,8)==3);      /* a link out of the stack stops it */
 stack[at[2]]=lo+8*at[3];
 assert(sct_walk(lo+8*4+4,lo,hi,out,8)==0);                       /* misaligned */
 assert(sct_walk(lo-16,lo,hi,out,8)==0 && sct_walk(hi-8,lo,hi,out,8)==0);  /* outside or overrunning the stack */
 assert(sct_walk(0,0,8,out,8)==0);                                /* a stack smaller than one record */
 puts("PASS: sampler frame walk stays inside the thread's stack, stops on a backward link, misalignment or max");
}
