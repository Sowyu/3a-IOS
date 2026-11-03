#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "BatteryRead.c"
int main(void){
 BatteryRead b={0};uint8_t p[24],r[24];int32_t values[]={1,1,7340,4200000,-450000,2875,1};
 for(unsigned i=0;i<BatteryFields;i++){
  assert(battery_read_request(&b,p)==24 && battery_read_allowed(p,24));memcpy(r,p,24);
  put32(r+12,properties[i]);put32(r+16,(uint32_t)values[i]);
  assert(!battery_read_receive(&b,r,24));assert(!battery_read_sent(&b));
  assert(!battery_read_request(&b,p));assert(!battery_read_receive(&b,r,23));
  put32(r+8,0x33);assert(!battery_read_receive(&b,r,24));put32(r+8,opcode(i));
  assert(battery_read_receive(&b,r,24)==1);assert(!battery_read_receive(&b,r,24));
 }
 assert(battery_read_valid(&b));assert(!battery_read_request(&b,p));
 b.value[2]=10001;assert(!battery_read_valid(&b));b.value[2]=7340;
 for(unsigned op=0;op<256;op++){put32(p+8,op);assert(!battery_read_allowed(p,24) || op==0x30 || op==0x32);}
 b=(BatteryRead){0};assert(!battery_read_sent(&b));battery_read_timeout(&b);assert(!battery_read_request(&b,p));assert(!battery_read_receive(&b,r,24));assert(!battery_read_valid(&b));
 puts("PASS: fixed GET allowlist, matched replies, complete snapshot and terminal timeout");
}
