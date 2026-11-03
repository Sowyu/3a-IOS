static void init(void){
 memset(&m,0,sizeof(m));shutdowns=reclaims=poweroffs=qmpoffs=unmaps=frees=sleeps=0;shut_result=reclaim_result=0;
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
 puts("PASS: actual SCM retry/resume bounds and shutdown/ownership failure cleanup paths");
}
