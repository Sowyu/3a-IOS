#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "IpaFirmware.h"
#include "ModemGate.h"
static void w16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void w32(uint8_t *p,uint32_t v){w16(p,v);w16(p+2,v>>16);}
static void fixture(uint8_t b[164]){
    memset(b,0,164);memcpy(b,"\177ELF\1\1\1",7);w16(b+16,2);w32(b+20,1);
    w32(b+28,52);w16(b+40,52);w16(b+42,32);w16(b+44,3);
    w32(b+52+16,148);
    uint8_t *p=b+84;w32(p,1);w32(p+16,16);w32(p+20,4096);w32(p+24,0x08000000);
    p=b+116;w32(p+16,16);w32(p+24,0x02000000);
}
int main(int argc,char **argv){
    uint8_t b[164];IpaFirmware fw;fixture(b);
    assert(!ipa_firmware_parse(b,sizeof(b),&fw));assert(fw.span==4096 && fw.count==1 && fw.file_bytes==16);
    for(size_t n=0;n<sizeof(b);n++)assert(ipa_firmware_parse(b,n,&fw));
    w32(b+84+20,0xa000);assert(!ipa_firmware_parse(b,sizeof(b),&fw));
    w32(b+84+20,0xa001);assert(ipa_firmware_parse(b,sizeof(b),&fw));
    fixture(b);w32(b+84+12,0xfffff000);w32(b+84+20,8192);assert(ipa_firmware_parse(b,sizeof(b),&fw));
    fixture(b);w32(b+84+20,8);assert(ipa_firmware_parse(b,sizeof(b),&fw));
    fixture(b);w32(b+116+16,15);assert(ipa_firmware_parse(b,sizeof(b),&fw));
    fixture(b);w32(b+84+24,0);assert(ipa_firmware_parse(b,sizeof(b),&fw));
    const char *bad[]={"","sweetipaprobe","sweetipaprobe=0","sweetipaprobe=01","sweetipaprobe=2","sweetipaprobe=1 sweetipaprobe=1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!modem_gate(bad[i],"sweetipaprobe"));
    assert(modem_gate("other=1 sweetipaprobe=1","sweetipaprobe"));
    if(argc==2){uint8_t actual[IPA_FW_METADATA_MAX+1];FILE *f=fopen(argv[1],"rb");assert(f);size_t n=fread(actual,1,sizeof(actual),f);assert(!ferror(f));fclose(f);
        assert(!ipa_firmware_parse(actual,n,&fw));printf("real MDT: loads=%u bytes=%u span=%#x\n",fw.count,fw.file_bytes,fw.span);}
    puts("IPA firmware bounds and strict probe gate PASS");
}
