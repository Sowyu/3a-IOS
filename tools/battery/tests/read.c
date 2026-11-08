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
 /* 2292 meter fields: three more GETs, 0x30/14, 0x32/1, 0x32/3; validity still from the first seven. */
 b=(BatteryRead){.fields=BatteryAllFields};int32_t meter[]={1,1,7340,4200000,450000,2875,1,4100000,5012000,812000};
 static const unsigned wantop[]={0x30,0x30,0x30,0x30,0x30,0x30,0x32,0x30,0x32,0x32},wantprop[]={0,2,4,7,9,12,0,14,1,3};
 for(unsigned i=0;i<BatteryAllFields;i++){
  assert(battery_read_request(&b,p)==24 && get32(p+8)==wantop[i] && get32(p+16)==wantprop[i] && !get32(p+12) && !get32(p+20));
  assert(!battery_read_sent(&b));memcpy(r,p,24);put32(r+12,wantprop[i]);put32(r+16,(uint32_t)meter[i]);
  if(i==BatteryFields-1){assert(!battery_read_valid(&b) && !battery_read_complete(&b));}
  assert(battery_read_receive(&b,r,24)==1);
 }
 assert(battery_read_complete(&b) && battery_read_valid(&b) && b.value[7]==4100000 && b.value[9]==812000 && !battery_read_request(&b,p));
 b.value[9]=-99999999;assert(battery_read_valid(&b));
 /* The allowlist grew by exactly those three: every other GET property and every other opcode stays refused. */
 unsigned allowed=0;
 for(unsigned op=0;op<256;op++)for(unsigned prop=0;prop<64;prop++){put32(p,32778);put32(p+4,1);put32(p+8,op);put32(p+12,0);put32(p+16,prop);put32(p+20,0);allowed+=battery_read_allowed(p,24);}
 assert(allowed==10);
 /* 2295/2296: notification parsing and the probe; the probe adds nothing to the allowlist. */
 {uint8_t n[16];uint32_t code=0;put32(n,32778);put32(n+4,2);put32(n+8,7);put32(n+12,0x80);assert(battery_notify_parse(n,16,&code) && code==0x80);
  assert(!battery_notify_parse(n,15,&code));put32(n+4,1);assert(!battery_notify_parse(n,16,&code));put32(n+4,2);put32(n+8,0x30);assert(!battery_notify_parse(n,16,&code));
  put32(n+8,7);put32(n,32779);assert(!battery_notify_parse(n,16,&code));
  uint32_t o,t,op;assert(battery_msg_header(n,16,&o,&t,&op) && o==32779 && t==2 && op==7 && !battery_msg_header(n,11,&o,&t,&op));}
 {uint8_t q[24],r2[24];int32_t on=-1;assert(battery_probe_request(q)==24 && battery_read_allowed(q,24) && get32(q+8)==0x32 && get32(q+16)==0 && !get32(q+12) && !get32(q+20));
  memcpy(r2,q,24);put32(r2+16,1);assert(battery_probe_receive(r2,24,&on)==1 && on==1);put32(r2+16,0);assert(battery_probe_receive(r2,24,&on)==1 && on==0);
  put32(r2+16,2);assert(battery_probe_receive(r2,24,&on)==-1);put32(r2+16,1);put32(r2+20,3);assert(battery_probe_receive(r2,24,&on)==-1);
  put32(r2+20,0);put32(r2+12,3);assert(!battery_probe_receive(r2,24,&on));put32(r2+12,0);put32(r2+8,0x30);assert(!battery_probe_receive(r2,24,&on));assert(!battery_probe_receive(r2,23,&on));}
 puts("PASS: meter fields add GET 0x30/14, 0x32/1 and 0x32/3 only, and never affect validity");

}
