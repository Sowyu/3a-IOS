static void reset(void){
 m=(typeof(m)){0};calls[0]=calls[1]=0;count=unmaps=frees=fail_pas=qfail=bits=clock_ms=retries=no_ack=vfail=votes=0;
 nmaps=kfrees=wrong_unmaps=0;
 m.images[0]=(struct adsp_image){36,true,false,fake_map(0x99280000,0x80000),fake_contig(META_MAX),0x80000,(void *)3,100};
 m.images[1]=(struct adsp_image){1,true,true,fake_map(0x99300000,0x2800000),fake_contig(META_MAX),0x2800000,(void *)6,100};m.sp.out=(void *)7;m.qmp_owed=true;
 /* The same physical addresses and sizes adsp_run maps. */
 m.smem=fake_map(0x81d00000,M_SM_SIZE);m.mutex=fake_map(0x1f43000,0x1000);m.ipcc=fake_map(0x405000,0x1000);
 m.qmpmem=fake_map(0xc300000,0x400);m.rsc=fake_map(0x17a20000,0x4000);m.db=fake_map(0x81c60000,0x20000);
}
int main(void){
 reset();assert(adsp_cleanup());assert(count==2 && calls[0]==1 && calls[1]==36 && bits==0 && unmaps==8 && kfrees==2 && !wrong_unmaps && !live_maps() && frees==2);
 reset();fail_pas=1;assert(!adsp_cleanup());assert(count==1 && calls[0]==1 && m.images[0].owed && m.images[1].owed && !unmaps && !kfrees && !frees && bits==0);
 reset();fail_pas=36;assert(!adsp_cleanup());assert(count==2 && !m.images[1].owed && m.images[0].owed && !unmaps && !kfrees && !frees);
 reset();retries=2;assert(adsp_cleanup());assert(count==4 && calls[0]==1 && calls[2]==1 && calls[3]==36);
 reset();retries=60;assert(!adsp_cleanup());assert(count==50 && !unmaps && !kfrees && !frees);
 reset();qfail=1;assert(!adsp_cleanup());assert(count==2 && !unmaps && !kfrees && !frees);
 reset();m.vote_uncertain=true;assert(!adsp_cleanup());assert(count==2 && !unmaps && !kfrees && !frees);
 reset();m.images[1].started=false;assert(adsp_cleanup());assert(count==2 && bits==0);
 reset();no_ack=1;assert(adsp_cleanup());assert(clock_ms==3000 && bits==0 && count==2 && unmaps==8 && kfrees==2 && !wrong_unmaps && !live_maps());
 reset();m.votes_owed=true;vfail=ETIMEDOUT;assert(!adsp_cleanup());assert(votes==1 && count==2 && !unmaps && !kfrees && !frees && !m.proxy_released);
 reset();m.votes_owed=m.proxy_released=true;assert(adsp_cleanup());assert(votes==0 && count==2 && unmaps==8 && kfrees==2 && !wrong_unmaps && !live_maps() && frees==2);
 reset();m.votes_owed=true;assert(adsp_cleanup());assert(votes==1 && m.proxy_released && unmaps==8 && kfrees==2 && !wrong_unmaps && !live_maps());
 puts("PASS: actual ADSP cleanup ordering, page-aligned exact unmaps, metadata by kmem_free, uncertain PAS retention, retry bound, QMP/TCS failures and stop clearing");
}
