#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "ModemUim.h"
static unsigned wrap(uint8_t *p,unsigned id,const uint8_t *v,unsigned n){
 memset(p,0,128);p[0]=2;p[1]=7;p[3]=id;p[5]=10+n;p[7]=2;p[8]=4;p[14]=0x10;p[15]=n;memcpy(p+17,v,n);return 17+n;
}
int main(void){
 uint8_t msg[128],v[64]={0};ModemUimStatus s;ModemUimSlots slots;unsigned n;
 v[8]=1;v[9]=1;v[14]=1;v[15]=2;v[16]=7;v[23]=2;
 n=wrap(msg,0x2f,v,29);assert(!modem_uim_status(msg,n,7,&s));assert(s.count==1 && s.card[0].state==1 && s.card[0].app[0].state==7 && s.card[0].app[0].pin1==2);
 assert(modem_uim_status(msg,n,8,&s)==-1 && !s.count);
 for(unsigned i=0;i<n;i++)assert(modem_uim_status(msg,i,7,&s)==-1);
 v[21]=33;n=wrap(msg,0x2f,v,29);assert(modem_uim_status(msg,n,7,&s)==-1);
 memset(v,0,sizeof(v));v[8]=1;n=wrap(msg,0x2f,v,15);assert(!modem_uim_status(msg,n,7,&s) && s.card[0].state==0);
 msg[10]=1;msg[12]=3;assert(modem_uim_status(msg,n,7,&s)==3 && !s.count);
 memset(v,0,sizeof(v));v[0]=1;v[1]=1;v[5]=1;v[9]=1;v[10]=10;memset(v+11,0x11,10);v[19]=0x21;v[20]=0x43;
 n=wrap(msg,0x47,v,21);assert(!modem_uim_slots(msg,n,7,&slots) && !strcmp(slots.slot[0].suffix,"1234"));
 for(unsigned i=0;i<n;i++)assert(modem_uim_slots(msg,i,7,&slots)==-1);
 v[20]=0xf3;n=wrap(msg,0x47,v,21);assert(!modem_uim_slots(msg,n,7,&slots) && !strcmp(slots.slot[0].suffix,"1123"));
 v[19]=0xf1;n=wrap(msg,0x47,v,21);assert(modem_uim_slots(msg,n,7,&slots)==-1 && !slots.count);
 char suffix[5];uint8_t bad[]={0xaa};assert(modem_uim_suffix(bad,1,suffix)==-1 && !suffix[0]);
 puts("PASS: UIM card/app status, absent/error, transaction/length checks and ICCID suffix-only BCD parsing");
}
