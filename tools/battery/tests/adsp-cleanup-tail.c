static void reset(void){
 m=(typeof(m)){0};calls[0]=calls[1]=0;count=unmaps=frees=fail_pas=qfail=bits=clock_ms=retries=no_ack=vfail=votes=0;
 m.images[0]=(struct adsp_image){36,true,false,1,2,0x80000,(void *)3,100};
 m.images[1]=(struct adsp_image){1,true,true,4,5,0x2800000,(void *)6,100};m.sp.out=(void *)7;m.qmp_owed=true;
}
int main(void){
 reset();assert(adsp_cleanup());assert(count==2 && calls[0]==1 && calls[1]==36 && bits==0 && unmaps==4 && frees==2);
 reset();fail_pas=1;assert(!adsp_cleanup());assert(count==1 && calls[0]==1 && m.images[0].owed && m.images[1].owed && !unmaps && !frees && bits==0);
 reset();fail_pas=36;assert(!adsp_cleanup());assert(count==2 && !m.images[1].owed && m.images[0].owed && !unmaps && !frees);
 reset();retries=2;assert(adsp_cleanup());assert(count==4 && calls[0]==1 && calls[2]==1 && calls[3]==36);
 reset();retries=60;assert(!adsp_cleanup());assert(count==50 && !unmaps && !frees);
 reset();qfail=1;assert(!adsp_cleanup());assert(count==2 && !unmaps && !frees);
 reset();m.vote_uncertain=true;assert(!adsp_cleanup());assert(count==2 && !unmaps && !frees);
 reset();m.images[1].started=false;assert(adsp_cleanup());assert(count==2 && bits==0);
 reset();no_ack=1;assert(adsp_cleanup());assert(clock_ms==3000 && bits==0 && count==2 && unmaps==4);
 reset();m.votes_owed=true;vfail=ETIMEDOUT;assert(!adsp_cleanup());assert(votes==1 && count==2 && !unmaps && !frees && !m.proxy_released);
 reset();m.votes_owed=m.proxy_released=true;assert(adsp_cleanup());assert(votes==0 && count==2 && unmaps==4 && frees==2);
 reset();m.votes_owed=true;assert(adsp_cleanup());assert(votes==1 && m.proxy_released && unmaps==4);
 puts("PASS: actual ADSP cleanup ordering, uncertain PAS retention, retry bound, QMP/TCS failures and stop clearing");
}
