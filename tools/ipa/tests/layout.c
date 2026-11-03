#include <assert.h>
#include <stdio.h>
#include "IpaLayout.h"
static uint8_t ram[0x4000];
int main(void){
 uint32_t off,n;assert(ipa_local_bounds(0x800,&off,&n) && off==0 && n==0x4000);
 assert(ipa_local_bounds(0x8000800,&off,&n) && off==0x4000 && n==0x4000);
 assert(!ipa_local_bounds(0x7fc,&off,&n));assert(!ipa_local_bounds(0x20000800,&off,&n));
 assert(ipa_layout_sram(ram)==13);uint32_t v;memcpy(&v,ram+0x280,4);assert(v==0xdeadbeef);assert(!ram[0x27f] && !ram[0x3fe7]);
 assert(ipa_layout_tables(ram,sizeof(ram),0x20000000));uint64_t q;memcpy(&q,ram+8,8);assert(q==0x484);memcpy(&q,ram+16+18*8,8);assert(q==0x20000000);
 uint8_t out[24];assert(ipa_layout_table_cmd(out,0x20000010,152,0x4000,0x488,0x528));memcpy(&q,out+8,8);assert((q&0xfff)==152);assert(((q>>12)&0xffff)==0x4488);assert(((q>>40)&0xffff)==0x4528);
 assert(!ipa_layout_table_cmd(out,0,152,0,0x3fe8,0));assert(!ipa_layout_table_cmd(out,0,152,0,0xffffffff,0));
 assert(ipa_layout_dma_cmd(out,0x2000a280,0x4000,0x280,0x3d68,false));uint16_t w[4];memcpy(w,out,8);assert(w[1]==0x3d68 && w[2]==0x4280 && !w[3]);
 assert(!ipa_layout_dma_cmd(out,0,0xffff,0x280,8,false));assert(!ipa_layout_dma_cmd(out,0,0,0x3fe8,1,false));
 puts("IPA5.2 SRAM bounds, canaries, unshifted filter bitmap and immediate encodings PASS");
}
