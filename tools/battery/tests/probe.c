#include "sweet_battery.c"
vm_offset_t ml_io_map_unmappable(vm_offset_t p,vm_size_t size,unsigned int flags){
 assert(flags==VM_WIMG_IO);
 maps++;if(failmap && maps==(unsigned)failmap)return 0;
 assert((p==0x81d00000 && size==sizeof(smem)) || (p==0x81c60000 && size==sizeof(db)));
 return (uintptr_t)(p==0x81d00000 ? smem : db);
}
void ml_io_unmap(vm_offset_t p,vm_size_t s){assert((p==(uintptr_t)smem && s==sizeof(smem)) || (p==(uintptr_t)db && s==sizeof(db)));unmaps++;}
void sweet_log_sync(void){}
kern_return_t thread_terminate(thread_t t){assert(t==1);return 0;}
static uint32_t saved[sizeof(smem)/4],saved_db[sizeof(db)/4];
static void w(unsigned at,uint32_t v){smem[at/4]=v;}
static void valid_memory(void){
 memset(smem,0,sizeof(smem)); memset(db,0,sizeof(db));
 unsigned toc=sizeof(smem)-0x1000;
 w(toc,0x434f5424);w(toc+4,1);w(toc+8,1);
 w(toc+32,0x1000);w(toc+36,0x1000);w(toc+44,2u<<16);w(toc+48,8);
 w(0x1000,0x54525024);w(0x1004,2u<<16);w(0x1008,0x1000);w(0x100c,0x188);w(0x1010,0xf00);
 w(0x1020,0xa5a5u|(443u<<16));w(0x1024,344);w(0x1028,4);
 w(0x1030,QCOM_SMP2P_MAGIC);w(0x1034,1);w(0x1038,2);w(0x103c,16|(1u<<16));
 memcpy((char *)smem+0x1044,"slave-kernel",13);w(0x1054,6);
 w(0x1ff0,0xa5a5u|(480u<<16));w(0x1ff4,0xf0);
 uint8_t *b=(uint8_t *)db;
 db[0]=1;db[1]=QCOM_CMD_DB_MAGIC;
 b[8]=1;b[12]=72;b[14]=3;
 const char *names[]={"lcx.lvl","lmx.lvl","xo.lvl"};
 for(unsigned i=0;i<3;i++){
  unsigned e=144+24*i;memcpy(b+e,names[i],7);
  uint32_t address=0x30000+4*i;memcpy(b+e+16,&address,4);
  b[e+20]=4;b[e+22]=(uint8_t)(4*i);b[216+4*i+2]=0x80;b[216+4*i+3]=1;
 }
}
static void valid_checks(void){
 valid_memory();
 QcomSmem m={(volatile uint8_t *)smem,sizeof(smem)};QcomSmemPartition p;
 assert(!qcom_smem_partition(&m,0,2,&p));
 uint32_t off,n;assert(!qcom_smem_find(&m,&p,443,&off,&n));assert(off==0x1030 && n==340);
 QcomSmp2p sp={0};sp.in=smem+off/4;assert(!qcom_smp2p_check_in(sp.in,0,2));
 int index=qcom_smp2p_find_in(&sp,"slave-kernel");assert(index==0 && qcom_smp2p_in_value(&sp,index)==6);
 assert(!qcom_smem_find(&m,&p,480,&off,&n));assert(off==0x1f00 && n==0xf0);
 const char *names[]={"lcx.lvl","lmx.lvl","xo.lvl"};
 for(unsigned i=0;i<3;i++){QcomRpmhResource r;assert(!qcom_cmd_db_find((uint8_t *)db,sizeof(db),names[i],&r));assert(r.level_count==2 && r.levels[1]==384 && r.address==0x30000+4*i);}
 memcpy(saved,smem,sizeof(smem));memcpy(saved_db,db,sizeof(db));
 starts=maps=unmaps=0;failmap=0;sweet_battery_start();assert(starts==1 && maps==2 && unmaps==2);
 assert(!memcmp(saved,smem,sizeof(smem)) && !memcmp(saved_db,db,sizeof(db)));
 w(0x1024,0xfffffff8);assert(qcom_smem_find(&m,&p,443,&off,&n)==QcomSmemCorrupt);
 w(0x1024,344);w(0x1028,345);assert(qcom_smem_find(&m,&p,443,&off,&n)==QcomSmemCorrupt);
 w(0x1028,0x10000004);assert(qcom_smem_find(&m,&p,443,&off,&n)==QcomSmemCorrupt);
 w(0x1028,4);w(0x1ff4,0x1000);assert(qcom_smem_find(&m,&p,480,&off,&n)==QcomSmemCorrupt);
 puts("PASS: valid uncached/cached items, SMP2P, three command-db resources, malformed extents");
}
int main(void){
 const char *off[]={"","sweetbprobe","sweetbprobe=0","sweetbprobe=2","sweetbprobe=01","sweetbprobe=1 sweetbprobe=1","sweetbprobe=1x"};
 for(unsigned i=0;i<sizeof(off)/sizeof(off[0]);i++){args=(char *)off[i];sweet_battery_start();assert(!starts && !maps);}
 args="sweetbprobe=1";
 for(int fail=0;fail<3;fail++){
  starts=maps=unmaps=0;failmap=fail;
  memset(smem,0xa5,sizeof(smem));memcpy(saved,smem,sizeof(smem));
  memcpy(saved_db,db,sizeof(db));
  sweet_battery_start();assert(starts==1 && maps==2 && unmaps==(fail?1u:2u));
  assert(!memcmp(saved,smem,sizeof(smem)));assert(!memcmp(saved_db,db,sizeof(db)));
 }
 valid_checks();
 puts("PASS: exact opt-in gate, mapping failures unwind, corrupt SMEM is read-only");
}
