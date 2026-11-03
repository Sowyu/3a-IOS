static void init(void){memset(&m,0,sizeof(m));memset(reg,0,sizeof(reg));m.rsc=1;m.smem=(uintptr_t)dummy;m.rail_addr[0]=0x100;m.rail_addr[1]=0x200;m.rail_index[0]=3;m.rail_index[1]=4;m.xo_addr=0x300;reg[0xd00/4]=5;mode=locked=lockcalls=triggers=xowrites=remote_state=0;}
int main(void){
 init();assert(!m_vote(true));assert(m.xo_pinned && m.votes_owed && !m.vote_uncertain && xowrites==1 && triggers==1 && reg[0xd00/4]==5 && !locked);
 remote_state=2;m_smp_poll();assert(triggers==1 && !m.proxy_released);
 remote_state=6;m_smp_poll();assert(triggers==2 && m.proxy_released && xowrites==1 && reg[0xd00/4]==5);
 m_smp_poll();assert(triggers==2);
 init();mode=1;assert(m_vote(true)==ETIMEDOUT);assert(m.vote_uncertain && reg[0xd2c/4]==7 && reg[0xd24/4] && !locked);
 remote_state=6;m_smp_poll();assert(triggers==1 && !m.proxy_released);
 init();mode=2;assert(m_vote(true)==ENODEV && !m.votes_owed && !m.xo_pinned && !triggers && !locked);
 init();assert(!m_vote(true));remote_state=7;m_smp_poll();assert(triggers==1 && m.fatal);
 init();assert(!m_vote(true));mode=3;remote_state=6;m_smp_poll();assert(m.failed && m.vote_uncertain && !m.proxy_released && triggers==2);
 m_smp_poll();assert(triggers==2);
 puts("PASS: actual ADSP votes and handover, both proxy rails released once, shared XO retained, failures propagated");
}
