#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "ModemRmtfs.h"
static void w(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
int main(void){
 uint8_t shadow[1024],window[1024],out[32],msg[128]={0};memset(shadow,0x55,sizeof(shadow));memset(window,0xaa,sizeof(window));
 ModemRmtfs r={.image={shadow,0,0,0},.bytes={sizeof(shadow),0,0,0},.window=window,.window_bytes=sizeof(window),.window_pa=0xe1000000};
 msg[3]=1;msg[5]=18;msg[7]=1;msg[8]=15;memcpy(msg+10,"/boot/modem_fs1",15);
 assert(modem_rmtfs_request(&r,msg,25,out,sizeof(out))==21 && !out[10]);
 memset(msg,0,sizeof(msg));msg[3]=3;msg[5]=27;msg[7]=1;msg[8]=4;msg[14]=2;msg[15]=1;msg[17]=0;msg[18]=3;msg[19]=13;msg[21]=1;w(msg+26,r.window_pa);w(msg+30,1);
 assert(modem_rmtfs_request(&r,msg,34,out,sizeof(out))==14 && !out[10]);assert(window[0]==0x55 && window[511]==0x55 && window[512]==0xaa);
 window[0]=0x77;msg[17]=1;assert(modem_rmtfs_request(&r,msg,34,out,sizeof(out))==14 && !out[10]);assert(shadow[0]==0x77 && r.writes==1);
 w(msg+22,UINT32_MAX);assert(modem_rmtfs_request(&r,msg,34,out,sizeof(out))==14 && out[10]);assert(r.writes==1);
 w(msg+22,0);w(msg+26,r.window_pa-1);assert(modem_rmtfs_request(&r,msg,34,out,sizeof(out))==14 && out[10]);
 w(msg+26,r.window_pa);msg[17]=2;assert(modem_rmtfs_request(&r,msg,34,out,sizeof(out))==14 && out[10]);
 assert(modem_rmtfs_request(&r,msg,33,out,sizeof(out))==0);
 puts("PASS: rmtfs open/read, writes only change RAM, invalid ranges/direction/truncation rejected");
}
