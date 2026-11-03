int main(void){
 ModemBcmCommand c[7],saved[7];init();assert(!modem_bcm_prepare(db,sizeof(db),c,7));memcpy(saved,c,sizeof(c));
 const unsigned order[]={5,3,4,1,2,0,6};
 for(unsigned i=0;i<7;i++){
  assert(c[i].resource==order[i]);assert(c[i].wait==(i==6 || c[i].vcd!=c[i+1].vcd));
  assert(!!(c[i].data&0x40000000)==c[i].wait);
  assert((c[i].data&0x0fffffff)==(c[i].resource==3?1:c[i].resource==6?3:0x0fffffff));
 }
 assert(!m_bandwidth_vote() && m.bandwidth_pinned && !m.failed && !m.vote_uncertain && triggers==1 && !locked);
 assert(reg[0xd00/4]==5 && !reg[0xd04/4] && !reg[0xd24/4] && !reg[0xd2c/4] && !reg[0xd20/4]);
 for(unsigned i=0;i<7;i++)assert(reg[(0xd38+i*24)/4]==c[i].address && reg[(0xd3c+i*24)/4]==c[i].data);
 init();m.bandwidth=false;assert(!m_bandwidth_vote() && !reads && !writes);
 init();m.vote_uncertain=true;assert(m_bandwidth_vote()==EBUSY && m.failed && !reads && !writes);
 init();reg[0xc/4]=6u<<27;assert(m_bandwidth_vote()==ENOTSUP && !writes);
 init();w16(db+16,0);assert(m_bandwidth_vote()==EINVAL && !writes);
 init();db[318]=9;assert(m_bandwidth_vote()==EINVAL && !writes);
 init();db[319]=1;assert(m_bandwidth_vote()==EINVAL && !writes);
 init();w32(db+312,0);assert(m_bandwidth_vote()==EINVAL && !writes);
 init();w16(db+164,7);assert(m_bandwidth_vote()==EINVAL && !writes);
 init();assert(modem_bcm_prepare(db,350,c,7));assert(!memcmp(c,saved,sizeof(c)));
 init();mode=1;assert(m_bandwidth_vote()==ETIMEDOUT && m.failed && m.vote_uncertain && !m.bandwidth_pinned && delays==100000);
 assert(reg[0xd24/4] && reg[0xd2c/4]==0x7f && reg[0xd20/4]==0x75 && !locked);
 unsigned oldwrites=writes;assert(m_bandwidth_vote()==EBUSY && writes==oldwrites);
 init();mode=2;assert(m_bandwidth_vote()==ETIMEDOUT && m.vote_uncertain && delays==100000);
 init();reg[0xd28/4]=0;assert(!m_bandwidth_vote() && !reg[0xd24/4] && reg[(0xd00+0x2a0+0x38)/4]==0x50000);
 init();reg[0xd28/4]=reg[(0xd28+0x2a0)/4]=0;assert(!m_bandwidth_vote() && reg[(0xd00+2*0x2a0+0x38)/4]==0x50000);
 init();reg[0xd28/4]=reg[(0xd28+0x2a0)/4]=reg[(0xd28+2*0x2a0)/4]=0;assert(m_bandwidth_vote()==ENODEV && !writes && m.failed);
 puts("PASS: measured BCM map, VCD grouping, saturation, actual RSC fresh completion, timeout retention, all TCS choices and default off");
}
