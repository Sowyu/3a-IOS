static void reset(void){m=(typeof(m)){0};m.reads=m.battery_up=true;m.state=6;now=1000;sent=published=cleared=0;send_result=0;}
/* 2295-2297 helpers: answer the outstanding request; run one full cycle with USB online = online. */
static const int32_t cyc[]={1,1,7340,4200000,450000,2875,1};
static void answer(int32_t value){uint8_t r[24];memcpy(r,request,24);put32(r+12,get32(request+16));put32(r+16,(uint32_t)value);put32(r+20,0);m_breceive(NULL,NULL,r,24);}
static void full_cycle(int32_t online){for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();assert(m.read.waiting && get32(request+16)==properties[i]);answer(i==6?online:cyc[i]);now++;}assert(battery_read_complete(&m.read));}
static void notify(uint32_t code){uint8_t r[16];put32(r,32778);put32(r+4,2);put32(r+8,7);put32(r+12,code);m_breceive(NULL,NULL,r,16);}
int main(void){
 assert(!adsp_stop_requested);uint8_t reply[24];int32_t values[]={1,1,7340,4200000,450000,2875,1};
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
 /* 2291 daily mode: a failed cycle keeps the client alive after a 30 s quiet period. */
 reset();m.daily=true;
 for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();assert(adsp_read_busy());memcpy(reply,request,24);put32(reply+12,properties[i]);put32(reply+16,(uint32_t)values[i]);m_breceive(NULL,NULL,reply,24);now++;}
 assert(published==1 && !adsp_read_busy() && m.cycle_due==now-1+20000);
 now=m.cycle_due;adsp_read_poll();assert(m.read.waiting && m.read.cursor==0);previous=sent;
 now+=2000;adsp_read_poll();assert(!m.failed && m.reads && m.read_failures==1 && cleared==1 && !m.read.waiting && !adsp_read_busy());
 memcpy(reply,request,24);put32(reply+12,0);put32(reply+16,1);m_breceive(NULL,NULL,reply,24);assert(m.read.cursor==0 && !m.read.waiting);
 now+=29999;adsp_read_poll();assert(sent==previous);
 now+=1;adsp_read_poll();assert(sent==previous+1 && m.read.waiting && m.read.cursor==0);
 memcpy(reply,request,24);put32(reply+12,0);put32(reply+16,1);put32(reply+20,7);m_breceive(NULL,NULL,reply,24);assert(!m.failed && m.read_failures==2);
 for(unsigned k=3;k<=5;k++){now=m.read_quiet_until;adsp_read_poll();assert(m.read.waiting);now+=2000;adsp_read_poll();assert(m.read_failures==k && !m.failed);}
 assert(!m.reads);previous=sent;now+=60000;adsp_read_poll();assert(sent==previous && !adsp_read_busy());
 /* An invalid but complete cycle is a read failure in daily mode, terminal in a test image. */
 for(int daily_mode=0;daily_mode<2;daily_mode++){
  reset();m.daily=daily_mode;int32_t bad[]={1,0,7340,4200000,450000,2875,1};
  for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();memcpy(reply,request,24);put32(reply+12,properties[i]);put32(reply+16,(uint32_t)bad[i]);m_breceive(NULL,NULL,reply,24);now++;}
  assert(!published && cleared==1 && m.failed==!daily_mode && (daily_mode?m.read_failures==1:1));
 }
 /* A successful cycle clears the failure count. */
 reset();m.daily=true;m.read_failures=4;
 for(unsigned i=0;i<BatteryFields;i++){adsp_read_poll();memcpy(reply,request,24);put32(reply+12,properties[i]);put32(reply+16,(uint32_t)values[i]);m_breceive(NULL,NULL,reply,24);now++;}
 assert(published==1 && !m.read_failures);
 /* Transport failures stay terminal in daily mode. */
 reset();m.daily=true;send_result=-99;adsp_read_poll();assert(m.failed && cleared==1);
 /* 2292 meter mode: ten requests per cycle and a 5 s cycle. */
 reset();m.daily=m.meter=true;adsp_read_reset();
 for(unsigned i=0;i<BatteryAllFields;i++){adsp_read_poll();assert(m.read.waiting && adsp_read_busy());memcpy(reply,request,24);put32(reply+12,properties[i]);put32(reply+16,(uint32_t)(i<7?values[i]:1000+i));m_breceive(NULL,NULL,reply,24);now++;}
 assert(published==1 && sent==BatteryAllFields && m.cycle_due==now-1+5000 && !adsp_read_busy());
 now=m.cycle_due;adsp_read_poll();assert(m.read.fields==BatteryAllFields && m.read.cursor==0 && m.read.waiting);
 /* A read failure keeps the meter fields on the fresh client. */
 now+=2000;adsp_read_poll();assert(m.read_failures==1 && m.read.fields==BatteryAllFields);
 /* 2295: a battery-manager notification starts one full read at once in daily mode, at most once per 5 s, with no
  * settle window, and never changes a value by itself. A regular cycle spends a pending cue. */
 reset();m.daily=true;now=60000;full_cycle(1);unsigned before=sent,pubs=published;
 now+=100;adsp_read_poll();assert(sent==before);notify(0x32);assert(m.notify_pending && sent==before && published==pubs);
 adsp_read_poll();assert(sent==before+1 && get32(request+16)==properties[0] && !m.notify_pending);
 for(unsigned i=0;i<BatteryFields;i++){if(i)adsp_read_poll();answer(cyc[i]);now++;}
 assert(published==pubs+1 && m.cycle_due==now-1+20000 && m.settle_until==0);
 notify(0x80);before=sent;now=m.last_notify_cycle+4999;adsp_read_poll();assert(sent==before);   /* within 5 s */
 now=m.last_notify_cycle+5000;adsp_read_poll();assert(sent==before+1);
 for(unsigned i=0;i<BatteryFields;i++){if(i)adsp_read_poll();answer(cyc[i]);now++;}
 /* A cue that arrived before a regular cycle started is spent by it: no extra read afterwards. */
 notify(0x80);m.last_notify_cycle=m.cycle_due-1000;now=m.cycle_due;adsp_read_poll();assert(!m.notify_pending && m.last_notify_cycle==now-1000);
 for(unsigned i=0;i<BatteryFields;i++){if(i)adsp_read_poll();answer(cyc[i]);now++;}
 before=sent;now+=6000;adsp_read_poll();assert(sent==before);
 /* A cue that arrives during a cycle is not: that cycle may have read the old state, so one more read follows. */
 now=m.cycle_due;adsp_read_poll();answer(cyc[0]);now++;notify(0x30);
 for(unsigned i=1;i<BatteryFields;i++){adsp_read_poll();answer(cyc[i]);now++;}
 assert(m.notify_pending);before=sent;now=m.last_notify_cycle+5000;if(now<m.cycle_started+1)now=m.cycle_started+1;adsp_read_poll();assert(sent==before+1 && !m.notify_pending);
 /* Not daily: logged, no trigger. A notification of the wrong size, type or owner is not a notification. */
 reset();full_cycle(1);before=sent;notify(0x32);adsp_read_poll();assert(sent==before && !m.notify_pending);
 {uint8_t r[16];put32(r,32778);put32(r+4,1);put32(r+8,7);put32(r+12,0x32);m.daily=true;m_breceive(NULL,NULL,r,16);assert(!m.notify_pending);
  put32(r,32779);put32(r+4,2);m_breceive(NULL,NULL,r,16);assert(!m.notify_pending);put32(r,32778);m_breceive(NULL,NULL,r,15);assert(!m.notify_pending);}
 /* 2296: between cycles, GET 0x32/0 every online_ms; the same answer does nothing, a change reads at once. */
 reset();m.daily=true;m.online_ms=1000;full_cycle(1);before=sent;
 m.online_due=now;adsp_read_poll();assert(sent==before+1 && m.probing && get32(request+8)==0x32 && get32(request+16)==0 && adsp_read_busy());
 adsp_read_poll();assert(sent==before+1);                            /* nothing else goes out while it waits */
 answer(1);assert(!m.probing && !m.refresh_pending && m.known_online==1);
 now=m.online_due;adsp_read_poll();assert(sent==before+2 && m.probing);answer(0);assert(m.known_online==0 && m.refresh_pending);
 adsp_read_poll();assert(sent==before+3 && !m.probing && get32(request+16)==properties[0] && get32(request+8)==0x30);
 for(unsigned i=0;i<BatteryFields;i++){if(i)adsp_read_poll();answer(i==6?0:cyc[i]);now++;}
 assert(m.known_online==0 && m.cycle_due==now-1+2000);
 /* No probe during a cycle: start the next cycle and check probes stay off until it completes. */
 now=m.cycle_due;m.online_due=0;adsp_read_poll();assert(m.read.waiting && !m.probing);before=sent;adsp_read_poll();assert(sent==before);
 /* Probe timeout and probe error are read failures (quiet 30 s), never terminal in daily mode. */
 reset();m.daily=true;m.online_ms=1000;full_cycle(1);m.online_due=now;adsp_read_poll();assert(m.probing);
 now+=2000;adsp_read_poll();assert(!m.probing && m.read_failures==1 && !m.failed && cleared==1);
 reset();m.daily=true;m.online_ms=1000;full_cycle(1);m.online_due=now;adsp_read_poll();
 {uint8_t r[24];memcpy(r,request,24);put32(r+16,1);put32(r+20,5);m_breceive(NULL,NULL,r,24);}assert(!m.probing && m.read_failures==1);
 reset();m.daily=true;m.online_ms=1000;full_cycle(1);m.online_due=now;adsp_read_poll();answer(7);assert(!m.probing && m.read_failures==1);
 /* Off (online_ms 0), before the first cycle, and outside daily mode: no probe. */
 reset();m.daily=true;full_cycle(1);m.online_due=0;before=sent;adsp_read_poll();assert(sent==before);
 reset();m.daily=true;m.online_ms=1000;m.online_due=0;adsp_read_poll();assert(get32(request+8)==0x30);
 reset();m.online_ms=1000;full_cycle(1);m.online_due=0;before=sent;adsp_read_poll();assert(sent==before);
 puts("PASS: notifications and USB-online changes start a full read at once, rate-limited, one request at a time, never a value from a notification");
 puts("PASS: daily mode keeps the ADSP up through read failures, drops late replies for 30 s, stops reading after 5, 20 s cycle");

}
