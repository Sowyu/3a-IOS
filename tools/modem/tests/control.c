#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ModemGate.h"
#include "QcomQmp.h"
#include "QcomSmp2p.h"
#include "QcomRpmh.h"
static unsigned kicks;
static void kick(void *p){(void)p;kicks++;}
static void w16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void w32(uint8_t *p,uint32_t v){w16(p,v);w16(p+2,v>>16);}
int main(void){
 assert(modem_gate("a=2 sweetmpss=1 b=3","sweetmpss"));
 const char *bad[]={"","sweetmpss","sweetmpss=0","sweetmpss=01","sweetmpss=4294967297","sweetmpss=1 sweetmpss=1","sweetmpss=1 sweetmpss=0","sweetmpss=1x","xsweetmpss=1"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!modem_gate(bad[i],"sweetmpss"));
 uint32_t ram[256]={0};QcomQmp q;ram[0]=0x4d41494c;ram[1]=1;ram[13]=128;ram[14]=256;
 qcom_qmp_init(&q,ram,sizeof(ram),kick,0);assert(qcom_qmp_step(&q)==QcomQmpLinkWait);
 ram[10]=0xffff;assert(qcom_qmp_step(&q)==QcomQmpChannelWait);ram[5]=0xffff;assert(qcom_qmp_step(&q)==QcomQmpAckWait);
 ram[12]=0xffff;assert(qcom_qmp_step(&q)==QcomQmpOpen);assert(qcom_qmp_send(&q,"{class: image}")==QcomQmpSending);
 assert(qcom_qmp_send(&q,"other")==QcomQmpBusy);ram[64]=0;assert(qcom_qmp_step(&q)==QcomQmpOpen);
 ram[14]=UINT32_MAX;qcom_qmp_init(&q,ram,sizeof(ram),kick,0);assert(qcom_qmp_step(&q)==QcomQmpBadDescriptor);
 qcom_qmp_init(&q,ram,8,kick,0);assert(qcom_qmp_step(&q)==QcomQmpBadDescriptor);
 uint32_t a[85],b[85];QcomSmp2p sa,sb;qcom_smp2p_init_out(&sa,a,0,1);qcom_smp2p_init_out(&sb,b,1,0);
 assert(qcom_smp2p_add_entry(&sa,"master-kernel")==0);assert(qcom_smp2p_add_entry(&sb,"slave-kernel")==0);
 sa.in=b;uint32_t value,changed;assert(qcom_smp2p_update(&sb,0,6,6));qcom_smp2p_service(&sa,"slave-kernel",&value,&changed);assert(value==6 && changed==6);
 assert(qcom_smp2p_update(&sa,-1,1,1)==0);b[3]=0xffff0010;assert(qcom_smp2p_check_in(b,0,1));
 uint8_t db[256]={0};w32(db+4,QCOM_CMD_DB_MAGIC);w16(db+8,3);w16(db+10,0);w16(db+12,32);w16(db+14,1);
 memcpy(db+144,"cx.lvl",6);w32(db+160,0x12340);w16(db+164,8);w16(db+176,0);w16(db+178,16);w16(db+180,256);w16(db+182,384);
 QcomRpmhResource r;uint32_t index;assert(!qcom_cmd_db_find(db,sizeof(db),"cx.lvl",&r));assert(!qcom_rpmh_level_index(&r,384,&index) && index==3);
 assert(r.slave_id==3 && r.aux_bytes==8 && !memcmp(r.aux_prefix,db+176,8));
 w16(db+164,1);assert(!qcom_cmd_db_find(db,sizeof(db),"cx.lvl",&r));assert(r.aux_prefix[0]==0 && r.aux_prefix[1]==0 && r.aux_bytes==1);
 w16(db+164,8);w16(db+166,79);assert(qcom_cmd_db_find(db,sizeof(db),"cx.lvl",&r)==QcomRpmhCorrupt);w16(db+166,0);
 w16(db+10,65535);assert(qcom_cmd_db_find(db,sizeof(db),"cx.lvl",&r)==QcomRpmhCorrupt);
 puts("PASS: strict stage gates, QMP handshake/bounds, SMP2P ready/handover and command-db bounds");
}
