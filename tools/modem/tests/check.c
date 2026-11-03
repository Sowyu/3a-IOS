#define MODEM_HOST_TEST 1
#include "sweet_modem.c"
static void w16(uint8_t *b,unsigned v) {b[0]=v;b[1]=v>>8;}
static void w32(uint8_t *b,uint32_t v) {w16(b,v);w16(b+2,v>>16);}
static void entry(uint8_t *p,const char *n,unsigned cl,unsigned size,bool dir) {
 memcpy(p,n,11);p[11]=dir?16:0;w16(p+26,cl);w32(p+28,size);
}
int main(void) {
 for (int i=-1;i<=5;i++) {if(i==1)continue;boot_level=i;sweet_modem_start();}
 boot_level=-99;sweet_modem_start(); assert(!threads && !reads);
 w16(disk+11,512);disk[13]=1;w16(disk+14,1);disk[16]=1;
 w16(disk+17,16);w16(disk+19,4200);w16(disk+22,17);disk[510]=0x55;disk[511]=0xaa;
 for(unsigned i=2;i<=4;i++)w16(disk+512+i*2,0xffff);
 entry(disk+18*512,"IMAGE      ",2,0,true);
 entry(disk+19*512,"MODEM   MDT",3,148,false);
 entry(disk+19*512+32,"MODEM   B02",4,1,false);
 uint8_t *m=disk+20*512;memcpy(m,"\177ELF",4);m[4]=1;m[5]=1;m[6]=1;
 w32(m+20,1);w32(m+28,52);w16(m+40,52);w16(m+42,32);w16(m+44,3);
 w32(m+52+32+24,2u<<24);
 uint8_t *p=m+52+64;w32(p,1);w32(p+12,0x80000000);w32(p+16,1);w32(p+20,4096);w32(p+24,1u<<27);
 modem_partition_bytes=sizeof(disk);struct wl_fat f;assert(!fat_mount(&f,"modem_a"));
 assert(!modem_mdt(m,148,&f));
 for(unsigned i=0;i<148;i++)assert(modem_mdt(m,i,&f));
 w32(p+16,4097);assert(modem_mdt(m,148,&f));w32(p+16,1);
 w32(p+20,0xe601000);assert(modem_mdt(m,148,&f));w32(p+20,4096);
 w32(m+28,0xfffffff0);assert(modem_mdt(m,148,&f));w32(m+28,52);
 w16(disk+19,1);assert(fat_mount(&f,"modem_a"));w16(disk+19,4200);
 w16(disk+22,1);assert(fat_mount(&f,"modem_a"));w16(disk+22,17);
 assert(!fat_mount(&f,"modem_a")); uint8_t byte;assert(fat_read_chain(&f,f.ncl+2,0,&byte,1));
 w16(disk+512+2*2,2);assert(!fat_mount(&f,"modem_a"));uint32_t entries;assert(fat_dir_load(&f,2,&entries));
 w16(disk+512+2*2,0xffff); boot_level=1;sweet_modem_start();assert(threads==1);
 assert(modem_read("modemst1",0,&byte,1));assert(modem_read("modem_a",UINT64_MAX,&byte,1));
 puts("PASS: off/invalid gates, FAT bounds/cycle, MDT truncation/overflow/span, partition allowlist");
}
