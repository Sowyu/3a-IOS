#include <assert.h>
#include <stdio.h>
#include "IpaSmmu.h"
static uint64_t words[IPA_PT_PAGES*512];
static uint64_t resolve(IpaPageTable *t,uint64_t va){
    unsigned page=0;unsigned shifts[]={30,21,12};uint64_t e=0;
    for(unsigned i=0;i<3;i++){
        e=words[page*512+((va>>shifts[i])&511)];if(!(e&1))return 0;
        if(i<2){assert((e&3)==3);page=(unsigned)(((e&0x0000fffffffff000ull)-t->physical)/4096);assert(page<t->used);}
    }return (e&0x0000fffffffff000ull)|(va&4095);
}
int main(void){
    IpaPageTable t;assert(!ipa_pt_init(&t,words,0x8c1000000ull));
    assert(!ipa_pt_map(&t,0x20000000,0x8c2000000ull,0x10000));
    assert(resolve(&t,0x20000123)==0x8c2000123ull);
    assert(!resolve(&t,0x1ffff000));assert(!resolve(&t,0x20010000));
    assert(!ipa_pt_map(&t,0x14683000,0x14683000,8192));
    assert(resolve(&t,0x14684fff)==0x14684fff);assert(!resolve(&t,0x14685000));
    assert(ipa_pt_map(&t,0x20000000,0x8c2000000ull,4096));
    assert(ipa_pt_map(&t,0x30000001,0x8c2000000ull,4096));
    assert(ipa_pt_map(&t,(1ull<<39)-4096,0,8192));
    assert(ipa_pt_map(&t,0x30000000,(1ull<<48)-4096,8192));
    assert(!ipa_pt_init(&t,words,0x8c1000000ull));
    unsigned mapped=0;while(!ipa_pt_map(&t,(uint64_t)mapped<<30,0x8c2000000ull,4096))mapped++;
    assert(mapped==7 && t.used<=IPA_PT_PAGES);
    puts("IPA isolated SMMU page tables PASS");
}
