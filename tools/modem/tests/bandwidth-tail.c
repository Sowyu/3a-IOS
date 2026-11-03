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
 init();boot_args="sweetmbw=1";assert(!m_bandwidth_vote());assert(!at_bcm("SN3"));assert((effective_sn3&0x0fffffff)==0x0fffffff && m.bandwidth_pinned);
 init();boot_args="sweetmbw=1";assert(!at_bcm("SN3"));assert(!m_bandwidth_vote());assert((effective_sn3&0x0fffffff)==0x0fffffff);
 const char *off[]={"","sweetmbw","sweetmbw=0","sweetmbw=01","sweetmbw=1 sweetmbw=1"};
 for(unsigned i=0;i<sizeof(off)/sizeof(off[0]);i++){init();boot_args=off[i];assert(!at_bcm("SN3"));assert((effective_sn3&0x0fffffff)==0x4001);}
 init();boot_args="sweetmbw=1";assert(!at_bcm("IP0"));assert(!effective_sn3 && reg[0xd3c/4]==0x60004001);
 init();mode=4;assert(m_bandwidth_vote()==ETIMEDOUT); /* Masked status model. */
 init();mode=4;m.bandwidth_irq=true;assert(!m_bandwidth_vote() && m.bandwidth_pinned && !m.vote_uncertain);
 init();mode=4;m.bandwidth_irq=true;gic_group=0;assert(m_bandwidth_vote()==EBUSY && !writes);
 init();mode=4;m.bandwidth_irq=true;gic_enable=1u<<5;assert(m_bandwidth_vote()==EBUSY && !writes);
 init();mode=4;m.bandwidth_irq=true;gic_active=1u<<5;assert(m_bandwidth_vote()==EBUSY && !writes);
 init();mode=4;m.bandwidth_irq=true;gic_group=0;gic_priority=0x80;assert(!m_bandwidth_vote() && m.bandwidth_pinned);
 init();mode=5;m.bandwidth_irq=true;assert(m_bandwidth_vote()==ENOMEM && !writes);
 init();mode=4;m.bandwidth_irq=true;reg[0xd00/4]=4;raw_irq=1;no_new_irq=true;
 assert(m_bandwidth_vote()==ETIMEDOUT && m.vote_uncertain && !m.bandwidth_pinned && reg[0xd00/4]==4);
 puts("PASS: measured BCM map, VCD grouping, saturation, actual RSC fresh completion, timeout retention, all TCS choices default off and touch order plus guarded unmasked completion");
}
