static void put32(uint8_t *b,uint32_t v){memcpy(b,&v,4);}
int main(void){
 uint8_t mdt[120]={0};put32(mdt+28,52);mdt[44]=2;
 put32(mdt+84,1);put32(mdt+84+12,0x1000);put32(mdt+84+16,8);put32(mdt+84+20,16);
 struct wl_fat f={0};
 for(unsigned failure=0;failure<7;failure++){
  struct adsp_image im={.stem="adsp",.pas=1,.base=0x99300000,.bytes=4096,.meta_pa=0xe1400000,.mdt=mdt,.mdt_bytes=sizeof(mdt)};
  active=&im;maps=pas_calls=fail_map=fail_pas=fail_file=0;memset(firmware,0xff,sizeof(firmware));
  if(failure==1)fail_map=1;if(failure==2)fail_map=2;
  if(failure>=3 && failure<=5)fail_pas=failure-2;if(failure==6)fail_file=1;
  int rc=adsp_load_image(&f,&im);
  if(!failure){assert(!rc && im.owed && im.started && pas_calls==3);for(unsigned i=0;i<8;i++)assert(firmware[i]==0x5a);for(unsigned i=8;i<16;i++)assert(firmware[i]==0);}
  else {assert(rc && !im.started);assert(im.owed==(failure!=1));}
 }
 puts("PASS: actual ADSP load marks shutdown owed before uncertain INIT, retains mappings on every failure, and zeros BSS");
}
