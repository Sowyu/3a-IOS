int main(void){
 /* Negative control for 2296: the series before it must not send a USB-online probe between cycles. */
 m=(typeof(m)){0};m.reads=m.battery_up=m.daily=true;m.state=6;now=1000;m.online_ms=1000;
 for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();uint8_t r[24];memcpy(r,request,24);put32(r+16,(uint32_t)(i==6?1:i==0?1:i==1?1:i==2?7340:i==3?4200000:i==4?450000:2875));m_breceive(NULL,NULL,r,24);now++;}
 unsigned before=sent;m.online_due=0;adsp_read_poll();
 assert(sent==before+1 && get32(request+8)==0x32 && "probe sent between cycles");
 return 0;
}
