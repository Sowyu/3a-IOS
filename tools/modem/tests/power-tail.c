int main(void){
 w32(db+4,QCOM_CMD_DB_MAGIC);w16(db+8,3);w16(db+12,72);w16(db+14,3);
 const char *names[]={"cx.lvl","mss.lvl","xo.lvl"};
 for(unsigned i=0;i<3;i++){uint8_t *e=db+144+24*i;memcpy(e,names[i],strlen(names[i]));w32(e+16,0x30000+16*i);w16(e+20,i==2?0:8);w16(e+22,i*8);w16(db+216+i*8+2,16);w16(db+216+i*8+4,256);w16(db+216+i*8+6,384);}
 assert(!m_power_inspect());assert(m.rail_addr[0]==0x30000 && m.rail_index[0]==3 && m.xo_addr==0x30020);
 w16(db+230,320);assert(!m_power_inspect() && m.rail_index[1]==3);w16(db+230,384);
 w16(db+230,128);assert(m_power_inspect()==EINVAL);w16(db+230,384);
 w16(db+188,64);assert(m_power_inspect()==EINVAL);w16(db+188,8);
 w16(db+188,7);assert(m_power_inspect()==EINVAL);w16(db+188,8);
 w16(db+188,2);assert(m_power_inspect()==EINVAL);w16(db+188,8);
 w32(db+208,0x30000);assert(m_power_inspect()==EINVAL);w32(db+208,0x30020);
 w32(db+4,0);assert(m_power_inspect()==EINVAL);
 puts("PASS: actual read-only power inspection, valid resources, 320 maximum clamping, malformed/truncated table and address/database refusal");
}
