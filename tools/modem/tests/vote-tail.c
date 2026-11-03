static void init(void){memset(&m,0,sizeof(m));memset(reg,0,sizeof(reg));m.rsc=1;m.rail_addr[0]=0x100;m.rail_addr[1]=0x200;m.rail_index[0]=3;m.rail_index[1]=4;m.xo_addr=0x300;reg[0xd00/4]=5;mode=locked=lockcalls=triggers=xowrites=0;}
int main(void){
 init();assert(!m_vote(true));assert(m.xo_pinned && m.votes_owed && !m.vote_uncertain && xowrites==1 && triggers==1 && reg[0xd00/4]==5 && !locked);
 assert(!m_vote(false));assert(xowrites==1 && triggers==2 && !reg[0xd2c/4] && !reg[0xd24/4] && reg[0xd00/4]==5);
 init();mode=1;assert(m_vote(true)==ETIMEDOUT);assert(m.vote_uncertain && reg[0xd2c/4]==7 && reg[0xd24/4] && !locked);
 init();mode=2;assert(m_vote(true)==ENODEV && !m.votes_owed && !m.xo_pinned && !triggers && !locked);
 puts("PASS: actual RSC votes acknowledge XO=3, never vote XO off, preserve IRQ bits and retain timed-out TCS");
}
