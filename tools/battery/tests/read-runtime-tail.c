static void reset(void){m=(typeof(m)){0};m.reads=m.battery_up=true;m.state=6;now=1000;sent=published=cleared=0;send_result=0;}
int main(void){
 uint8_t reply[24];int32_t values[]={1,1,7340,4200000,-450000,2875,1};
 reset();m.reads=false;adsp_read_poll();assert(!sent);m.reads=true;m.battery_up=false;adsp_read_poll();assert(!sent);m.battery_up=true;m.state=2;adsp_read_poll();assert(!sent);m.state=6;
 for(unsigned i=0;i<BatteryFields;i++){
  adsp_read_poll();assert(sent==i+1 && m.read.waiting);memcpy(reply,request,24);put32(reply+12,properties[i]);put32(reply+16,(uint32_t)values[i]);
  m_breceive(NULL,NULL,reply,23);assert(m.read.waiting);m_breceive(NULL,NULL,reply,24);assert(!m.read.waiting);now++;
 }
 assert(published==1 && !m.failed);unsigned previous=sent;adsp_read_poll();assert(sent==previous);
 now=m.cycle_due;adsp_read_poll();assert(sent==previous+1 && m.read.waiting);
 now+=2000;adsp_read_poll();assert(m.failed && cleared==1);adsp_read_poll();assert(sent==previous+1);
 reset();send_result=QcomGlinkAgain;adsp_read_poll();assert(!sent && !m.read.waiting && !m.failed);now+=20000;adsp_read_poll();assert(m.failed && cleared==1);
 reset();send_result=-99;adsp_read_poll();assert(m.failed && cleared==1 && !m.read.waiting);
 reset();adsp_read_poll();memcpy(reply,request,24);put32(reply+12,0);put32(reply+16,1);put32(reply+20,1);m_breceive(NULL,NULL,reply,24);assert(m.failed && cleared==1);
 puts("PASS: actual GET sender/receiver, gate dependencies, refresh, backpressure bound and failure invalidation");
}
