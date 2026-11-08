int main(void){
 /* Negative control for 2295: the series before it must not start a read on a notification. */
 m=(typeof(m)){0};m.reads=m.battery_up=m.daily=true;m.state=6;now=1000;
 for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();uint8_t r[24];memcpy(r,request,24);put32(r+16,(uint32_t)(i==6?1:i==0?1:i==1?1:i==2?7340:i==3?4200000:i==4?450000:2875));m_breceive(NULL,NULL,r,24);now++;}
 unsigned before=sent;now+=100;uint8_t n[16];put32(n,32778);put32(n+4,2);put32(n+8,7);put32(n+12,0x32);m_breceive(NULL,NULL,n,16);adsp_read_poll();
 assert(sent==before+1 && "notification starts a read");
 return 0;
}
