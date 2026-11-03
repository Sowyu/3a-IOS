static void init(void){
 memset(&m,0,sizeof(m));shutdowns=reclaims=poweroffs=qmpoffs=unmaps=frees=sleeps=0;shut_result=shut_status=reclaim_result=0;mode=4;stop_updates=transient_failures=0;stop_asserted=no_ack=graceful=false;now=0;
 m.pas_owed=m.started=m.shared_owed=m.qmp_owed=m.votes_owed=true;
 m.sp.out=(volatile uint32_t *)1;m.firmware=m.metadata=m.shared=m.args=1;m.fs.image[0]=(void *)1;m.fs.bytes[0]=512;
}
int main(void){
 uint64_t result=0;mode=1;assert(!m_scm(0x42000201,2,4,5,6,7,&result));assert(result==9 && rawcalls==2);
 rawcalls=0;mode=2;assert(m_scm(0x42000201,2,4,5,6,7,&result)==ETIMEDOUT);assert(rawcalls==4);
 rawcalls=0;mode=3;assert(m_scm(0x42000201,2,4,5,6,7,&result)==ETIMEDOUT);assert(rawcalls==256);
 init();assert(m_cleanup());assert(shutdowns==1 && reclaims==1 && poweroffs==1 && qmpoffs==1 && unmaps==4 && frees==1);
 init();shut_result=-1;assert(!m_cleanup());assert(shutdowns==1 && !reclaims && !poweroffs && !qmpoffs && !unmaps && !frees);
 init();reclaim_result=-1;assert(!m_cleanup());assert(shutdowns==1 && reclaims==1 && !unmaps && !frees);
 init();m.started=false;assert(m_cleanup());assert(shutdowns==1);
 init();m.vote_uncertain=true;assert(!m_cleanup());assert(shutdowns==1 && !poweroffs && !unmaps && !frees);
 init();shut_result=-22;assert(!m_cleanup());assert(shutdowns==50 && !unmaps && !frees);
 init();shut_status=-3145800;transient_failures=2;assert(m_cleanup());assert(shutdowns==3 && sleeps==2 && !stop_asserted);
 init();shut_status=-3145800;assert(!m_cleanup());assert(shutdowns==50 && !reclaims && !unmaps && !frees);
 init();shut_status=-1;assert(!m_cleanup());assert(shutdowns==1 && !reclaims && !unmaps);
 init();shut_status=-4;assert(!m_cleanup());assert(shutdowns==1 && !unmaps);
 init();shut_status=-5;assert(!m_cleanup());assert(shutdowns==1 && !unmaps);
 init();no_ack=true;assert(m_cleanup());assert(now==3000 && stop_updates==2 && !stop_asserted);
 init();graceful=true;assert(m_cleanup());assert(shutdowns==1 && stop_updates==0 && reclaims==1);
 init();m.failed=true;assert(!m_cleanup());assert(shutdowns==1 && unmaps==4);
 init();m.fatal=true;assert(!m_cleanup());assert(shutdowns==1 && unmaps==4);
 puts("PASS: actual SCM retry/resume bounds and shutdown/ownership failure cleanup paths");
}
