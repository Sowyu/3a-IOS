#include <assert.h>
#include <stdio.h>
#include "IpaTxQueue.h"
int main(void){
 IpaTxQueue q={0};uint8_t frame[2049];memset(frame,0x55,sizeof(frame));
 assert(ipa_tx_copy(&q,frame,16)==1);q.ready=true;assert(ipa_tx_copy(&q,0,16)==2);assert(ipa_tx_copy(&q,frame,2049)==3);
 assert(!ipa_tx_copy(&q,frame,2048));memset(frame,0xcc,sizeof(frame));assert(q.packet[0].data[0]==0x55 && q.packet[0].data[2047]==0x55);
 for(unsigned n=1;n<32;n++)assert(!ipa_tx_copy(&q,frame,4));assert(ipa_tx_copy(&q,frame,4)==4);
 for(unsigned n=0;n<100;n++){q.head=(q.head+1)%32;q.count--;assert(!ipa_tx_copy(&q,frame,16));}
 ipa_tx_drop_all(&q);assert(!q.count && !q.ready && ipa_tx_copy(&q,frame,16)==1);
 puts("IPA TX copies caller memory, bounds the queue and drops pending work on stop PASS");
}
