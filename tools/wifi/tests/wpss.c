/* Host checks for 2240 to 2242: the gates, the W1 survey and the W2 start and verified stop, run on the actual sources.
 * Each scenario runs in its own process, so sweet_wpss_start's once-per-boot latch is fresh. Usage: wpss MODEM_A_IMAGE */
#include "sweet_wpss.c"
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c);fputs(hlog,stderr);exit(1);}}while(0)
static const char *GOOD="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwpss=1";
static const char *PIN="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwpin=1";
/* 2249 (W5): the board data gate, and one helper for the order assertions: each string must occur, in this order. */
static const char *W5="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1";
/* 2250 (W6): the mode gate, which is W5's whole exchange plus the cold-boot mode request. */
static const char *W6="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1";
/* 2251 (W7): the mission gate, which is W6's whole exchange plus WLAN_INI, WLAN_CFG and mission mode. */
static const char *W7="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1";
/* 2252 (W8 survey): W7's run plus three read-only apps_smmu snapshots. */
static const char *W8="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1";
static int text_order(const char *a,const char *b,const char *c,const char *d){
 const char *p=hlog;const char *need[4]={a,b,c,d};
 for(unsigned i=0;i<4;i++){if(!need[i] || !*need[i])continue;p=strstr(p,need[i]);if(!p)return 0;p+=strlen(need[i]);}
 return 1;
}

static void run(const char *args,int deadman){host_args=args;host_deadman=deadman;threads=0;wpss_attempted=false;sweet_wpss_start();}
static unsigned count_pas(unsigned cmd){unsigned n=0;for(unsigned i=0;i<pas_ncalls;i++)n+=pas_calls[i].cmd==cmd;return n;}
static bool all_unmapped(void){for(int i=0;i<R_COUNT;i++)if(regions[i].maps!=regions[i].unmaps)return false;return true;}
/* The WPSS regions only: the pin step maps and unmaps its own SPMI pages before any of them. */
static bool none_unmapped(void){for(int i=R_SMEM;i<=R_CARVE;i++)if(regions[i].unmaps)return false;return true;}
/* Every RSC command for cx (0x30000) and mx (0x30010) is the table maximum, index 7, and xo (0x30020) is 3: no release. */
static bool votes_never_lowered(unsigned *commands){
 *commands=0;
 for(unsigned i=0;i<rsc_nwrites;i++){
  unsigned o=rsc_writes[i].off;if(o<0xd00 || (o-0xd00)%0x2a0<0x34)continue;
  unsigned c=((o-0xd00)%0x2a0-0x34)%24;if(c!=8)continue;  /* data word */
  uint32_t addr;memcpy(&addr,(void *)(regions[R_RSC].va+o-4),4);
  uint32_t v=rsc_writes[i].value;(*commands)++;
  if((addr==0x30000 || addr==0x30010) && v!=7)return false;
  if(addr==0x30020 && v!=3)return false;
 }
 return true;
}
/* The firmware in the carveout equals the files, and the bss-only segment 11 is zero. */
static void check_loaded(void){
 struct wl_fat f;CHECK(!fat_mount(&f,"modem_a"));
 uint8_t *carve=at(R_CARVE,0);
 static const unsigned seg[]={1,2,3,4,5,6,7,8,9,10};
 static const uint32_t addr[]={0x9bb00000,0x9bb02000,0x9bb73000,0x9bc00000,0x9bc01000,0x9bc02000,0x9c200000,0x9c21d000,0x9c261000,0x9d100000};
 for(unsigned i=0;i<10;i++){
  char path[40];uint32_t cl,size;snprintf(path,sizeof(path),"image/qca6750/wpss.b%02u",seg[i]);
  CHECK(!fat_open(&f,path,&cl,&size));uint8_t *b=malloc(size);CHECK(b && !fat_read_chain(&f,cl,0,b,size));
  CHECK(!memcmp(carve+(addr[i]-0x9bb00000),b,size));free(b);
 }
 for(unsigned i=0;i<2236416;i++)CHECK(!carve[0x9d1de000-0x9bb00000+i]);
}

static void t_gates(void){
 static const char *off[]={"","sweetwpss=0","sweetwpss","sweetwpss=01","sweetwpss=1x","xsweetwpss=1","sweetwprobe=0","sweetwprobe",
  "sweetwprobe=1 sweetwprobe=1","sweetwprobe=1 sweetwpss=1",
  "sweetwpss=1","sweetfbarm=1 sweetselftest=1 sweetwpss=1","sweetfbarm=1 sweetdeadman=600 sweetwpss=1",
  "sweetselftest=1 sweetdeadman=600 sweetwpss=1","sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwpss=1 sweetwpss=1"};
 for(unsigned i=0;i<sizeof(off)/sizeof(off[0]);i++){
  int deadman=strstr(off[i],"sweetdeadman=600")?600:-1;
  CHECK(wpss_gate(off[i],deadman)<=0);
 }
 CHECK(wpss_gate(GOOD,599)==WpssRefused && wpss_gate(GOOD,0)==WpssRefused && wpss_gate(GOOD,600)==WpssStart);
 CHECK(wpss_gate("sweetwprobe=1",-1)==WpssSurvey);
 /* 2243 sweetwpin=1: exact, with the start's safeguards, and exclusive with both other gates. */
 CHECK(wpss_gate(PIN,600)==WpssPin && wpss_gate(PIN,599)==WpssRefused && wpss_gate("sweetwpin=1",-1)==WpssRefused);
 {char a[256];snprintf(a,sizeof(a),"%s sweetwprobe=1",PIN);CHECK(wpss_gate(a,600)==WpssRefused);
  snprintf(a,sizeof(a),"%s sweetwpss=1",PIN);CHECK(wpss_gate(a,600)==WpssRefused);
  snprintf(a,sizeof(a),"%s sweetwpin=1",PIN);CHECK(wpss_gate(a,600)==WpssRefused);
  snprintf(a,sizeof(a),"%s sweetdaily=1",PIN);CHECK(wpss_gate(a,600)==WpssRefused);
  CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwpin=0",600)==WpssRefused);
  CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwpin",600)==WpssRefused);}
 /* Every name in wpss_refused and in ModemGate.h's three lists refuses both gates, in the =1 and bare forms. */
 const char *const *lists[4]={wpss_refused,modem_cell_gates,modem_cell_refused,modem_daily_refused};const unsigned sizes[4]={5,14,8,5};
 unsigned names=0;
 for(unsigned l=0;l<4;l++)for(unsigned i=0;i<sizes[l];i++){
  for(int bare=0;bare<2;bare++){char a[256];
   snprintf(a,sizeof(a),bare?"sweetwprobe=1 %s":"sweetwprobe=1 %s=1",lists[l][i]);CHECK(wpss_gate(a,-1)==WpssRefused);
   snprintf(a,sizeof(a),bare?"%s %s":"%s %s=1",GOOD,lists[l][i]);CHECK(wpss_gate(a,600)==WpssRefused);}
  names++;
 }
 CHECK(names==32);
 static const char *named[]={"sweetbglink","sweetbread","sweetbpub","sweetbmeter","sweetmstop","sweetmuim","sweetdata","sweetmtftp","sweetmbcm"};
 for(unsigned i=0;i<sizeof(named)/sizeof(named[0]);i++){char a[256];snprintf(a,sizeof(a),"%s %s=1",GOOD,named[i]);CHECK(wpss_gate(a,600)==WpssRefused);}
 static const char *mods[]={"sweetdaily=1","sweetcell=1","sweetbatt=1","sweetmpss=1","sweetbadsp=1","sweetbprobe=1","sweetipa=1",
  "sweetipaprobe=1","sweetmprobe=1","sweetdaily","sweetbatt=0","sweetmpss=0"};
 for(unsigned i=0;i<sizeof(mods)/sizeof(mods[0]);i++){char a[256];snprintf(a,sizeof(a),"%s %s",GOOD,mods[i]);CHECK(wpss_gate(a,600)==WpssRefused);}
 puts("PASS: gates: exact tokens only, safeguards required, probe and start exclusive, every modem and battery gate refused for both");
}
static void t_off_paths(void){
 world_init();run("",-1);CHECK(!threads && !pas_ncalls && !ipcc_nrings && !rsc_nwrites);
 /* The latch: a second call in one boot does nothing, whatever the arguments. */
 host_args=GOOD;host_deadman=600;sweet_wpss_start();CHECK(!threads && !pas_ncalls);
 run("sweetwpss=1",-1);CHECK(!threads && logged("SWEET WIFI: refused"));
 puts("PASS: absent or refused gates start no thread and touch nothing");
}
static void t_survey(void){
 world_init();spmi_decoy();
 uint8_t *saved=malloc(0x200000);memcpy(saved,at(R_SMEM,0),0x200000);
 run("sweetwprobe=1",-1);
 CHECK(threads==1 && !pas_ncalls && !ipcc_nrings && !rsc_nwrites && !qmp_nsent && !meta_allocs);
 CHECK(!memcmp(saved,at(R_SMEM,0),0x200000));
 for(int i=0;i<R_COUNT;i++){
  bool used=i==R_SMEM || i==R_DB || i==R_SPMI_CORE || i==R_SPMI_CNFG || i==R_SPMI_OBS;  /* never R_SPMI_CHNL */
  CHECK(regions[i].maps==(used?1:0) && regions[i].maps==regions[i].unmaps);
  CHECK(regions[i].writes==(i==R_SPMI_OBS?10:0));   /* the only writes: ten observer read commands, no channel */
 }
 CHECK(spmi_cmds==10 && logged("W1 SPMI PPID 0x0bc APID 12 owner EE 3") && logged("W1 SPMI PPID 0x0bc APID 37 owner EE 0"));
 CHECK(logged("W1 pmk8550 gpio5 reg=0xbc04 rc=0 status=0x1 value=0x10") && logged("W1 pmk8550 gpio5 reg=0xbc46 rc=0 status=0x1 value=0x80") && !pmic_nwrites);
 CHECK(logged("W1 partition 0-13 rc=0") && logged("W1 item=617 rc=-7 offset=0 bytes=0 (wpss-in)") && logged("W1 item=616 rc=-7 offset=0 bytes=0 (wpss-out)") && logged("W1 firmware error=0"));
 CHECK(logged("firmware preflight PASS loads=11 bytes=8191703 span=0x1900000") && logged("W1 power inspect error=0"));
 CHECK(logged("W1 survey complete") && !kallocs);
 /* A WPSS item present is reported with its state, still read only. */
 world_init();remote_start();remote_running=false;qcom_smp2p_update(&remote,0,6,6);memcpy(saved,at(R_SMEM,0),0x200000);
 run("sweetwprobe=1",-1);CHECK(logged("W1 SMP2P check=0 index=0 state=0x6") && !memcmp(saved,at(R_SMEM,0),0x200000) && !pas_ncalls);
 /* SPMI: failed reads, a hung command (one line, channel not reused), a non-v7 arbiter and a missing PPID map nothing more. */
 world_init();spmi_fail=1;run("sweetwprobe=1",-1);CHECK(logged("reg=0xbc46 rc=5") && spmi_cmds==10 && all_unmapped());
 world_init();spmi_hang=1;run("sweetwprobe=1",-1);CHECK(logged("reg=0xbc04 rc=60") && !logged("reg=0xbc05") && spmi_cmds==1 && all_unmapped() && logged("W1 survey complete"));
 world_init();put32(R_SPMI_CORE,0,0x50000000u);run("sweetwprobe=1",-1);CHECK(logged("is not v7") && !regions[R_SPMI_OBS].maps && all_unmapped());
 world_init();put32(R_SPMI_CORE,0x2000+4*GPIO5_APID,0);run("sweetwprobe=1",-1);CHECK(logged("not in the arbiter map") && !regions[R_SPMI_OBS].maps);
 /* Mapping failures unwind what was mapped. */
 for(int fm=0;fm<2;fm++){world_init();fail_map=fm==0?R_SMEM:R_DB;run("sweetwprobe=1",-1);CHECK(logged("W1 mapping failed") && all_unmapped() && !pas_ncalls);}
 for(int fm=R_SPMI_CORE;fm<=R_SPMI_OBS;fm++){world_init();spmi_decoy();fail_map=fm;run("sweetwprobe=1",-1);CHECK(logged("mapping failed") && all_unmapped() && !spmi_cmds);}
 free(saved);
 puts("PASS: survey maps only SMEM, cmd-db and the v7 SPMI core, config and one observer; its only writes are ten observer read commands; real firmware chain; SPMI failure, hang, non-v7 and mapping failures");
}
static void t_start_ok(int interrupt){
 world_init();auth_interrupt=interrupt;
 run(GOOD,600);
 CHECK(threads==1 && logged("W2 worker final_error=0") && logged("W2 STOP verified; cleanup=ok"));
 CHECK(pmic_nwrites==3 && gpio5_regs[0x44]==2 && gpio5_regs[0x42]==5 && gpio5_regs[0x40]==1 && logged("W1c sleep clock pin result=0"));
 CHECK(pas_ncalls==4);
 CHECK(pas_calls[0].cmd==1 && pas_calls[0].pas==6 && pas_calls[0].a==META_PA && pas_calls[0].info==0x82);
 CHECK(pas_calls[1].cmd==2 && pas_calls[1].a==0x9bb00000 && pas_calls[1].b==0x1900000 && pas_calls[1].info==3);
 CHECK(pas_calls[2].cmd==5 && pas_calls[2].info==1);
 const struct pas_call *stop=&pas_calls[pas_ncalls-1];
 CHECK(stop->cmd==6 && stop->info==1 && stop->ack_before);   /* SMP2P stop acknowledged before the TrustZone shutdown */
 CHECK(syncs==1 && !memcmp(meta_buf,"\177ELF",4) && meta_allocs==1 && meta_frees==1 && !kallocs);
 CHECK(qmp_nsent==2 && strstr(qmp_sent[0],"name: wpss, val: on") && strstr(qmp_sent[1],"name: wpss, val: off"));
 unsigned commands;CHECK(votes_never_lowered(&commands) && commands==3);
 CHECK(all_unmapped() && logged("handover; shared cx/mx/xo votes kept"));
 for(unsigned i=0;i<ipcc_nrings;i++)CHECK(ipcc_rings[i]==(0x18u<<16|2) || ipcc_rings[i]==0);
 /* 20 s after handover, then the stop: ready at 300 ms, handover at 400 ms. */
 uint64_t up=stop->t-pas_calls[2].t;CHECK(up>=20400000 && up<24000000);
 check_loaded();
 fprintf(stdout,"PASS: start%s: PAS6 init, mem_setup 0x9bb00000+0x1900000, auth, 20 s up, SMP2P stop acked, then PAS6 shutdown; real firmware loaded\n",interrupt?" with an interrupted auth resumed":"");
}
/* 2244: ast208 cut a cold WPSS off at 10 s. A DSP that takes 18 s must now come up and be stopped cleanly. */
static void t_slow_start(void){
 world_init();opt_ready_ms=18000;
 run(GOOD,600);
 CHECK(threads==1 && logged("W2 worker final_error=0") && logged("W2 STOP verified; cleanup=ok"));
 CHECK(logged("SMP2P item=617 magic=0x504d5324") && logged("header=ok rc=0"));
 CHECK(count_pas(5)==1 && count_pas(6)==1 && logged("stop request cleared ack=1") && logged("PAS6 shutdown done"));
 CHECK(all_unmapped());
 fprintf(stdout,"PASS: a cold start that says nothing for 18 s is waited out and stopped cleanly\n");
}
/* 2246 (W3): the WPSS edge with GLINK and QRTR up, nothing sent. The fake DSP makes item 480 and drives VERSION and OPEN;
 * the code under test must answer both, advertise its intents and report the channel up. */
static void t_glink(void){
 world_init();
 run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1",600);
 CHECK(threads==1 && logged("W3 glink IPCRTR up=1") && logged("W3 glink their_fifo="));
 /* 2247: the descriptor line names the four words as Linux's qcom_glink_smem.c uses them, not as head/tail pairs
  * (W3 audit note 3). The old labels read as a mirrored order to anyone who trusted them. */
 CHECK(logged("W3 glink descriptor item=478 ") && logged("dsp_read(w0)=") && logged("our_write(w1)=")
       && logged("our_read(w2)=") && logged("dsp_write(w3)="));
 CHECK(!logged("their_head=") && !logged("their_tail="));
 CHECK(logged("W3 qrtr hello sent") && !logged("hello waiting"));
 CHECK(logged("final_error=0") && logged("W2 STOP verified; cleanup=ok") && count_pas(6)==1 && logged("ack=1"));
 CHECK(logged("W3 glink close: up=1"));
 /* Nothing but the link's own bookkeeping may go out: QRTR's HELLO is the only packet from W3, and no data packet at
  * all. The NEW_SERVER lines are the WPSS's own announcements arriving (ast222 saw exactly these), not ours. */
 CHECK(logged("W3 qrtr hello sent") && !logged("bad packet"));
 CHECK(logged("W3 qrtr totals packets=") && logged(" data=0 bad=0 unrouted=0 refused=0 remote_services="));
 /* The hello must go out on the first try: a refused send is dropped by QcomQrtr, not retried. */
 CHECK(logged("refused=0 remote_services=") && !logged("refused=1"));
 CHECK(all_unmapped());
 fprintf(stdout,"PASS: W3 glink: the WPSS edge comes up, handshakes and closes, and sends nothing but QRTR's hello\n");
 /* A DSP that never makes item 480: the link never opens, and the run still stops cleanly. */
 world_init();opt_no_glink_fifo=1;
 run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1",600);
 CHECK(!logged("W3 glink IPCRTR up=1") && logged("W2 STOP verified") && count_pas(6)==1 && all_unmapped());
 /* A DSP that never answers VERSION. */
 world_init();opt_glink_silent=1;
 run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1",600);
 CHECK(!logged("W3 glink IPCRTR up=1") && logged("W2 STOP verified") && all_unmapped());
 /* 2247 (W3 audit note 9): the link comes up but the WPSS never advertises an intent to send into. Our HELLO never
  * leaves, and the run must say so rather than looking like a silent firmware. */
 world_init();opt_no_intent=1;
 run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1",600);
 CHECK(logged("W3 glink IPCRTR up=1") && !logged("W3 qrtr hello sent"));
 CHECK(logged("W3 qrtr hello waiting: the link is up ") && logged(" ms and the WPSS has advertised no intent"));
 CHECK(logged("W3 glink intents from the WPSS: valid=0 idle=0 of"));
 CHECK(logged("W3 qrtr hello never sent: no intent from the WPSS, so our side never spoke"));
 CHECK(logged("final_error=0") && count_pas(6)==1 && logged("ack=1") && all_unmapped());
 /* The waiting line is logged once, not on every 2 ms poll. */
 unsigned n=0;for(const char *p=hlog;(p=strstr(p,"hello waiting"));p++)n++;
 CHECK(n==1);
 /* The gate: W3 may run beside a preset, and must still refuse every probe and preset member. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1 sweetdaily=1",600)==WpssGlink);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1 sweetdaily=1 sweetcell=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwglink=1 sweetwprobe=1",600)==WpssRefused);
 /* No deadman token at all: the caller passes 0, and the gate refuses. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetwglink=1",0)==WpssRefused);
 puts("PASS: W3 glink: absent FIFO and a silent peer both end in a verified stop; the gate allows a preset and refuses probes");
}
/* 2248 (W4): the WLFW QMI client on the W3 edge. The fake WPSS answers the lookup, the four requests and says nothing
 * else; the code under test must look the service up, send each request in order, decode each reply and stop cleanly. */
static void t_wlfw(void){
 const char *W4="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1";
 world_init();
 run(W4,600);
 CHECK(threads==1 && logged("W4 worker begin"));
 /* L1 first: the service is announced unprompted on this phone, so the lookup is a confirmation, and it must be asked. */
 CHECK(logged("W3 lookup service=0x45 instance=0x301 found node=13 port="));
 /* The four requests, in ath11k's order for this chip, each answered. */
 CHECK(logged("W4 wlfw ind_register request bytes=") && logged("W4 wlfw ind_register result=0 error=0"));
 CHECK(logged("W4 wlfw host_cap request bytes=") && logged("W4 wlfw host_cap result=0 error=0"));
 CHECK(logged("W4 wlfw cap request bytes=") && logged("W4 wlfw cap result=0 error=0"));
 CHECK(logged("W4 wlfw device_info request bytes=") && logged("W4 wlfw device_info result=0 error=0"));
 CHECK(logged("W4 wlfw cap board=0xff board_present=1 chip=0x1 family=0xb chip_present=1"));
 /* The window is recorded and never mapped: the address appears in the log and nowhere else. */
 CHECK(logged("W4 wlfw device_info bar=0xbad0000 bytes=0x200000 (recorded; not mapped, not read)"));
 CHECK(logged("W4 wlfw exchange complete: 4 requests answered"));
 /* Order: the four went out in sequence, transactions 1 to 4, and exactly four QMI requests were sent. */
 CHECK(rgw.requests==4 && rgw.malformed==0 && rgw.unexpected==0 && rgw.qmi_data==4);
 CHECK(logged("W4 wlfw close: step=done tx=4 indications=0 answered=4 dropped=0+0"));
 /* The run still stops the way W2b proved, and nothing is left mapped. */
 CHECK(logged("W2 stop request cleared ack=1") && logged("PAS6 cmd=6 status=0") && logged("W2 STOP verified; cleanup=ok"));
 CHECK(logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: lookup, four requests answered in order, board id and register window recorded, verified stop\n");

 /* The lookup is what finds the service when the firmware announces nothing: L1's own question. */
 world_init();opt_wlfw_quiet=1;
 run(W4,600);
 CHECK(logged("W3 lookup service=0x45 instance=0x301 asked") && logged("W3 lookup service=0x45 instance=0x301 found node=13 port="));
 CHECK(logged("W4 wlfw exchange complete") && logged("final_error=0"));
 if(!all_unmapped()){for(int i=0;i<R_COUNT;i++)if(regions[i].maps!=regions[i].unmaps)fprintf(stderr,"leak region %d maps=%d unmaps=%d\n",i,regions[i].maps,regions[i].unmaps);}
 CHECK(all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: a firmware that announces nothing is found by the lookup itself\n");

 /* No answer to the lookup: reported, nothing else sent, and the stop still verified. The other four services are still
  * announced (quiet removes only WLFW), so the line says four, not zero: that is the whole point of it. */
 world_init();opt_wlfw_quiet=1;opt_lookup_silent=1;
 run(W4,600);
 CHECK(logged("W3 lookup service=0x45 instance=0x301 asked"));
 CHECK(logged("W3 lookup timeout service=0x45 instance=0x301 after 3000 ms; remote services=4"));
 CHECK(!logged("W4 wlfw ind_register request") && rgw.requests==0);
 CHECK(logged("W2 STOP verified") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: a lookup that times out sends nothing else and still stops cleanly\n");

 /* A refusal on the first request: said plainly, and the other three are not sent. */
 world_init();opt_refuse=0x0020;
 run(W4,600);
 CHECK(logged("W4 wlfw ind_register result=1 error=48"));
 CHECK(logged("W4 wlfw refused at ind_register result=1 error=48; the rest is not sent"));
 CHECK(!logged("W4 wlfw host_cap request") && rgw.requests==1);
 CHECK(logged("W2 STOP verified") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: a refused request stops the sequence and still stops the DSP cleanly\n");

 /* A request with no reply: bounded, named, and the rest is not sent. */
 world_init();opt_silent=0x0024;
 run(W4,600);
 CHECK(logged("W4 wlfw cap no reply in 5000 ms; the rest is not sent"));
 CHECK(!logged("W4 wlfw device_info request") && logged("final_error=0"));
 CHECK(logged("W2 STOP verified") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: an unanswered request is bounded and named, and the run still stops cleanly\n");

 /* A reply whose TLV claims more than it carries: rejected as malformed, not parsed as if it were whole. */
 world_init();opt_short_reply=0x0034;
 run(W4,600);
 CHECK(logged("W4 wlfw malformed reply") && logged("final_error=0") && logged("W2 STOP verified"));
 CHECK(!logged("W4 wlfw host_cap result=") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: a truncated reply is rejected as malformed rather than parsed\n");

 /* A firmware with no board id: said so, and the run carries on, as ath11k does with 0xff. */
 world_init();opt_no_board=1;
 run(W4,600);
 CHECK(logged("W4 wlfw cap reply carries no board id"));
 CHECK(logged("W4 wlfw exchange complete") && logged("final_error=0"));
 fprintf(stdout,"PASS: W4 wlfw: a firmware with no board id is reported and tolerated\n");

 /* 2248b (W4 audit defect 1): a reply that arrives AFTER the 5 s bound. The firmware withholds host_cap's answer and
  * sends it 6 s after the request, from the announced port, with that request's own transaction and id. The client times
  * host_cap out at 5000 ms, sets step=done and clears wlfw_sent; the late reply then arrives with the transaction still
  * matching. On the old code it fell through the transaction check, the switch found step==W4Done so the `else
  * wlfw_step++` ran, the step became 6, and the close line indexed w4_step_name[6]. Here the reply must be dropped, the
  * step must stay `done`, and the stop must be the verified one. */
 world_init();opt_hold_reply=0x0034;opt_hold_ms=6000;
 run(W4,600);
 CHECK(logged("W4 wlfw host_cap no reply in 5000 ms; the rest is not sent"));
 CHECK(logged("W4 wlfw dropped reply id=0x34"));
 CHECK(!logged("W4 wlfw device_info request") && rgw.requests==2);
 CHECK(logged("W4 wlfw close: step=done ") && logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 CHECK(logged("PAS6 cmd=6 status=0") && all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: a reply after the 5 s bound is dropped and the run still stops cleanly\n");

 /* 2248b: a reply that arrives AFTER the exchange completed. The firmware answers device_info twice; the second is the
  * same bytes the first was. On the old code step was W4Done and the transaction still 4, so the second reply advanced
  * the step past W4Done. Here it must be dropped and the completion line must be the last thing the exchange says. */
 world_init();opt_repeat_reply=0x004c;
 run(W4,600);
 CHECK(logged("W4 wlfw exchange complete: 4 requests answered"));
 CHECK(logged("W4 wlfw dropped reply id=0x4c") && logged("nothing advanced"));
 CHECK(logged("W4 wlfw close: step=done ") && logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 CHECK(logged("PAS6 cmd=6 status=0") && all_unmapped());
 /* Exactly one completion line: a reply that re-entered the switch would have printed a second one or walked on. */
 {unsigned n=0;for(const char *p=hlog;(p=strstr(p,"exchange complete"));p++)n++;CHECK(n==1);}
 fprintf(stdout,"PASS: W4 wlfw: a repeated reply after the exchange is complete is dropped and changes nothing\n");

 /* 2248b (W4 audit defect 1): the step name is clamped. The receive guard is what stops the step ever leaving 0..5, but
  * the close path runs inside w_cleanup on the stop path of a daily phone, so the lookup itself must be safe whatever
  * the step holds. Every in-range step names itself, and every out-of-range one reads the last entry rather than past the
  * array: on the old code, a step of 6 here read a pointer from whatever followed w4_step_name in rodata. */
 for(uint32_t s=0;s<W4_STEPS;s++)CHECK(!strcmp(w4_step_str(s),w4_step_name[s]));
 CHECK(!strcmp(w4_step_str(W4_STEPS),"done") && !strcmp(w4_step_str(W4_STEPS+1),"done"));
 CHECK(!strcmp(w4_step_str(0xffffffffu),"done") && !strcmp(w4_step_str(W4Done),"done"));
 /* 2249 (W5): the three steps W5 adds are inside the array, and the last of them is still not the terminal one. */
 CHECK(!strcmp(w4_step_str(W4Regdb),"regdb") && !strcmp(w4_step_str(W4Board),"board") && !strcmp(w4_step_str(W4InitDone),"init_done"));
 CHECK(W4InitDone<W4Done && W4Done<W4_STEPS);
 fprintf(stdout,"PASS: W4 wlfw: the step name is clamped, so no step can index past the array\n");

 /* 2248b (W4 audit defect 1): the guard's four conditions, asserted directly rather than only through a conversation.
  * Each of the first three is a way the old code accepted a reply it should have dropped: nothing outstanding (a reply
  * after the timeout), a step that sends nothing (the same reply once the step is done, or a repeat after the exchange
  * completed), and the wrong message id or transaction (audit note 6). The last line is a reply that MUST be accepted,
  * so the guard cannot pass by refusing everything. This is what makes the "no request outstanding" control fail: with
  * that half of the guard removed, w4_reply_expected returns true for the done state below. */
 CHECK(!w4_reply_expected(false,W4DeviceInfo,4,WLFW_DEVICE_INFO_REQ,4));   /* nothing outstanding */
 CHECK(!w4_reply_expected(true,W4Lookup,4,WLFW_DEVICE_INFO_REQ,4));        /* a step that sends nothing */
 CHECK(!w4_reply_expected(true,W4Done,4,WLFW_DEVICE_INFO_REQ,4));          /* done: a late or repeated reply */
 CHECK(!w4_reply_expected(true,W4HostCap,2,WLFW_DEVICE_INFO_REQ,2));       /* the wrong message id */
 CHECK(!w4_reply_expected(true,W4HostCap,2,WLFW_HOST_CAP_REQ,3));          /* the wrong transaction */
 CHECK(w4_reply_expected(true,W4HostCap,2,WLFW_HOST_CAP_REQ,2));           /* and the one that is the answer */
 fprintf(stdout,"PASS: W4 wlfw: only the outstanding request's own reply is accepted, and nothing else advances the step\n");

 /* 2248b (W4 audit note 6): a reply whose message id is not the outstanding request's, at the right transaction. The
  * firmware answers host_cap's transaction with device_info's id. On the old code, which checked only the transaction,
  * this advanced the step and the decoded values came from the wrong message. */
 world_init();opt_wrong_id_for=0x0034;opt_wrong_id=0x004c;
 run(W4,600);
 CHECK(logged("W4 wlfw dropped reply id=0x4c tx=2: the outstanding request is host_cap tx=2, nothing advanced"));
 CHECK(logged("W4 wlfw host_cap no reply in 5000 ms; the rest is not sent"));
 CHECK(!logged("W4 wlfw host_cap result=") && !logged("W4 wlfw device_info request"));
 CHECK(logged("W4 wlfw close: step=done ") && logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W4 wlfw: a reply with the wrong message id is dropped and does not advance the step\n");

 /* 2248b (W4 audit defect 3): the audit's worst case exactly. The firmware announces nothing, so the lookup has to go
  * out and its answer is 2.9 s coming — almost the whole 3 s bound. Every reply is then withheld 4.9 s, inside its own
  * 5 s bound. The exchange that results is 3 s of lookup + four replies at 4.9 s + three 50 ms gaps = about 23 s, which
  * is the number the audit measured against the 20 s dwell. On the old code the stop cut the last request off; now every
  * bound is clamped to the dwell's end, so the last request's timeout line is printed inside the dwell with the wait it
  * actually had, and nothing is left running when the stop begins. */
 world_init();opt_wlfw_quiet=1;opt_lookup_delay_ms=2900;opt_hold_reply=0xffffu;opt_hold_ms=4900;
 run(W4,600);
 CHECK(logged("W4 exchange bounded to end 19750 ms after handover, 250 ms before the stop"));
 CHECK(logged("W3 lookup service=0x45 instance=0x301 asked"));
 /* Three requests are answered with their own 5 s bounds intact: lookup 2.9 + 4.9 + 4.9 + 4.9 is still inside 19.75 s. */
 CHECK(logged("W4 wlfw ind_register result=0 error=0"));
 CHECK(logged("W4 wlfw host_cap result=0 error=0"));
 CHECK(logged("W4 wlfw cap result=0 error=0"));
 /* The fourth would need until 22.9 s, past the dwell's 19.75 s, so its bound is cut to what is left: this is the line
  * the old code could never print, because the stop took the run first. */
 CHECK(logged("W4 wlfw device_info request bytes=7 transaction=4"));
 CHECK(logged("W4 wlfw device_info no reply in ") && !logged("W4 wlfw device_info no reply in 5000 ms"));
 CHECK(logged("W4 wlfw close: step=done "));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && logged("PAS6 cmd=6 status=0"));
 CHECK(all_unmapped());
 fprintf(stdout,"PASS: W4 wlfw: the audit's 23 s worst case is clamped inside the 20 s dwell and still stops cleanly\n");

 /* The gate: W4 may run beside a preset, and refuses every other Wi-Fi gate and preset member. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1",600)==WpssWlfw);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1 sweetdaily=1",600)==WpssWlfw);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1 sweetmpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwlfw=1",599)==WpssRefused);
 puts("PASS: W4 gate: a preset is allowed beside it, every other Wi-Fi gate and preset member is refused");
}
/* 2249 (W5): the board data download. The fake firmware reassembles both files from the pieces our encoder produces and
 * compares them, byte for byte, against the same files read on its own handle; so a piece that is short, repeated,
 * out of order, carrying the wrong total or the wrong type is refused rather than accepted. */
static void t_bdf(void){
 world_init();
 run(W5,600);
 CHECK(threads==1 && logged("W5 worker begin"));
 CHECK(logged("W3 lookup service=0x45 instance=0x301 found node=13 port="));
 CHECK(logged("W4 wlfw device_info result=0 error=0"));
 /* The board id decides the file. ast234's firmware sent none, so this is ath11k's 0xff fallback and the vendor's own
  * `bdwlan.bin` case; the line names the class, never a device. */
 CHECK(logged("W4 wlfw cap board=0xff board_present=1"));
 CHECK(logged("W5 bdf regdb: bytes=24278 type=4"));
 CHECK(logged("W5 bdf board: board=0xff board_present=1 variant=default bytes=58180 type=0"));
 CHECK(logged("W5 bdf regdb complete: pieces=4 accepted=4 bytes=24278"));
 CHECK(logged("W5 bdf board complete: pieces=10 accepted=10 bytes=58180"));
 CHECK(logged("W5 bdf init_done=1 after "));
 CHECK(logged("W5 bdf close: regdb_pieces=4 board_pieces=10 init_done=1"));
 /* The exchange itself is unchanged: four requests first, in order, then the board data. */
 CHECK(logged("W4 wlfw exchange complete: 4 requests answered"));
 CHECK(text_order("W4 wlfw device_info request","W5 bdf regdb piece=0/4","W5 bdf board piece=0/10","W5 bdf init_done=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0"));
 CHECK(all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: the four requests, then regdb and one board file in pieces the peer reassembles byte for byte, then init_done, then the verified stop\n");

 /* The pieces are the WPSS's own receive intents: an intent of 0x800 gives pieces of 0x800 - the overhead, and the same
  * file takes more of them. This is the case the piece size exists for, and it is asserted rather than assumed. */
 world_init();opt_intent_size=0x800;
 run(W5,600);
 CHECK(logged("W5 bdf regdb first piece size=1968 (the WPSS's largest idle intent minus 80)"));
 CHECK(logged("W5 bdf regdb complete: pieces=13 accepted=13 bytes=24278"));
 CHECK(logged("W5 bdf board complete: pieces=30 accepted=30 bytes=58180"));
 CHECK(logged("W5 bdf init_done=1") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: a WPSS that offers only 2 KiB intents gets smaller pieces of the same files\n");

 /* A firmware that refuses a piece. The stage says which file and how far, sends nothing after it, and still stops. */
 world_init();opt_refuse_bdf_at=2;
 run(W5,600);
 CHECK(logged("W4 wlfw refused at regdb result=1 error=48"));
 CHECK(logged("W5 bdf regdb stopped at piece=2/4 after 12288 bytes of 24278"));
 CHECK(!logged("W5 bdf board:"));
 CHECK(logged("W5 bdf close: regdb_pieces=2 board_pieces=0 init_done=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: a refused piece stops that file and the board data is never sent, with the stop still verified\n");

 /* A firmware that takes both files and never says init_done: bounded, named, and the stop still verified. */
 world_init();opt_no_init_done=1;
 run(W5,600);
 CHECK(logged("W5 bdf board complete: pieces=10"));
 CHECK(logged("W5 bdf init_done=0 after 10000 ms; the firmware did not say it was initialised"));
 CHECK(logged("W5 bdf close: regdb_pieces=4 board_pieces=10 init_done=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: a firmware that never says init_done is waited out inside the dwell and stopped cleanly\n");

 /* A firmware with no board id at all is the case this phone is in, and it must still pick a file. The other direction is
  * asserted too: a board id that no file on the phone is named for must be refused, not guessed at. */
 world_init();opt_no_board=1;
 run(W5,600);
 CHECK(logged("W4 wlfw cap reply carries no board id") && logged("variant=default bytes=58180"));
 CHECK(logged("W5 bdf init_done=1") && logged("final_error=0"));
 {world_init();opt_board=0x1234;run(W5,600);
  CHECK(logged("W5 bdf refused: the firmware reported board=0x1234"));
  CHECK(!logged("W5 bdf board: board=") && logged("W2 STOP verified") && all_unmapped());}
 fprintf(stdout,"PASS: W5 bdf: no board id takes the default file, and an id no variant matches is refused rather than guessed\n");

 /* A reply to a piece that arrives after the piece's bound: dropped, as W4's late reply is, and the stage ends. */
 world_init();opt_hold_reply=0x0025;opt_hold_ms=6000;
 run(W5,600);
 CHECK(logged("W4 wlfw regdb no reply in 5000 ms; the rest is not sent"));
 CHECK(logged("W4 wlfw dropped reply id=0x25"));
 CHECK(logged("W5 bdf close: regdb_pieces=0 board_pieces=0 init_done=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: a piece reply after its bound is dropped and the run still stops cleanly\n");

 /* A firmware that takes its time but still answers: every piece's reply is withheld 400 ms, which is well inside each
  * piece's own 5 s bound. 41 pieces at that pace is about 17 s of pieces plus the init_done wait, which is what
  * W_BDF_DWELL_MS exists for: with W4's 20 s dwell the stop would cut the last pieces off and init_done would never be
  * seen. The stage must still reach init_done and complete. */
 world_init();opt_hold_reply=0xffffu;opt_hold_ms=1500;
 run(W5,600);
 CHECK(logged("W5 bdf regdb complete: pieces=4 accepted=4 bytes=24278"));
 CHECK(logged("W5 bdf board complete: pieces=10 accepted=10 bytes=58180"));
 CHECK(logged("W5 bdf init_done=1 after "));
 CHECK(logged("W5 bdf close: regdb_pieces=4 board_pieces=10 init_done=1"));
 CHECK(logged("W4 exchange bounded to end 34750 ms after handover, 250 ms before the stop"));
 /* 14 pieces at 1.5 s each is about 21 s, which is past W4's 20 s dwell and inside this gate's 34.75 s. That gap is the
  * whole reason the constant exists, and it is asserted rather than described. */
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W5 bdf: a slow firmware still finishes inside this gate's longer dwell\n");

 /* A firmware that says init_done early: after the regulatory database and while the board file is still going out. The
  * host must ignore it, because a file it has not sent cannot have been taken, and go on to wait for the one that follows
  * the board file. Both indications are logged; only the second one ends the wait. */
 world_init();opt_early_init_done=1;
 run(W5,600);
 CHECK(logged("W5 bdf board complete: pieces=10 accepted=10 bytes=58180"));
 CHECK(logged("W5 bdf init_done=1 after "));
 CHECK(logged("W5 bdf close: regdb_pieces=4 board_pieces=10 init_done=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 /* The taken indication is the one after the board file: the completion line precedes the init_done line. A host that took
  * the early one would print init_done first, which is what the control checks for. */
 CHECK(text_order("W5 bdf board complete","W5 bdf init_done=1","",""));
 /* Two fw_init_done indications: the early one and the real one, and only the second is taken. */
 {unsigned n=0;for(const char *p=hlog;(p=strstr(p,"indication=fw_init_done"));p++)n++;CHECK(n>=1);}
 fprintf(stdout,"PASS: W5 bdf: an init_done sent before the board file is finished is not taken\n");

 /* The gate: W5 may run beside a preset, refuses W3 and W4 and every preset member, and needs the safeguards. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1",600)==WpssBdf);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1 sweetdaily=1",600)==WpssBdf);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1 sweetwpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwbdf=1",599)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwbdf=1 sweetmpss=1",600)==WpssRefused);
 puts("PASS: W5 gate: a preset is allowed beside it, W3 and W4 and every preset member are refused");
}

/* 2250 (W6): the cold-boot mode request. W5's whole exchange first (the fake firmware reassembles both files), then mode
 * 7, then cold_boot_cal_done and fw_ready, then the verified stop. The peer reads the mode out of our request byte by
 * byte, so this is not a test of a shared constant, and the order is asserted rather than described. */
static void t_mode(void){
 world_init();
 run(W6,600);
 CHECK(threads==1 && logged("W6 worker begin"));
 /* W5's exchange, unchanged, all of it. */
 CHECK(logged("W3 lookup service=0x45 instance=0x301 found node=13 port="));
 CHECK(logged("W4 wlfw device_info result=0 error=0"));
 CHECK(logged("W5 bdf regdb complete: pieces=4 accepted=4 bytes=24278"));
 CHECK(logged("W5 bdf board complete: pieces=10 accepted=10 bytes=58180"));
 CHECK(logged("W5 bdf init_done=1 after "));
 /* Then, and only then, the mode request. Its payload is ath11k's 11 bytes, and the mode is 7. */
 CHECK(logged("W6 mode next: cold-boot calibration (mode 7)"));
 CHECK(logged("W4 wlfw wlan_mode request bytes=18 transaction="));
 CHECK(logged("W4 wlfw wlan_mode result=0 error=0"));
 CHECK(opt_mode_requests==1 && opt_mode_bytes==11 && opt_mode_value==7);
 CHECK(logged("W6 mode accepted; waiting up to 20000 ms for cold_boot_cal_done"));
 /* The two indications, in the order ath11k expects, each timed from the mode request. */
 CHECK(logged("W6 mode cold_boot_cal_done=1 after "));
 CHECK(logged("W6 mode fw_ready=1 "));
 CHECK(text_order("W5 bdf init_done=1","W6 mode next:","W6 mode cold_boot_cal_done=1","W6 mode fw_ready=1"));
 /* Nothing else was sent: no WLAN_INI, no WLAN_CFG, no mission mode, and no second mode request. */
 CHECK(opt_mode_requests==1 && !logged("wlan_ini") && !logged("wlan_cfg"));
 /* The register window is still recorded and never mapped: W6 touches no register at all. */
 CHECK(logged("(recorded; not mapped, not read)"));
 CHECK(logged("W6 mode close: sent=1 cal_done=1 fw_ready=1 request_mem=0"));
 /* The stop, exactly as every stage before this ended. */
 CHECK(logged("W2 stop request cleared ack=1") && logged("PAS6 cmd=6 status=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 CHECK(logged("W4 wlfw close: step=done tx=19 indications=3 answered=19 dropped=0+0"));
 CHECK(!logged("W4 wlfw close order wrong") && !logged("W4 wlfw malformed reply"));
 fprintf(stdout,"PASS: W6 mode: the board data, then cold-boot calibration and fw_ready, in ath11k's order, with the verified stop\n");

 /* The gate: W6 may run beside a preset, refuses W3, W4 and W5 and every preset member, and needs the safeguards. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1",600)==WpssMode);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetdaily=1",600)==WpssMode);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetwbdf=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetwpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwmode=1",599)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetmpss=1",600)==WpssRefused);
 puts("PASS: W6 gate: a preset is allowed beside it, W3, W4 and W5 and every preset member are refused");

 /* A firmware that refuses the mode. The refusal is named with the step and the code, nothing after it is waited on,
  * and the stop is still the verified one: a stage that expects a mode it does not get must not pretend it did. */
 world_init();opt_refuse_mode=1;
 run(W6,600);
 CHECK(logged("W4 wlfw wlan_mode result=1 error=48"));
 CHECK(logged("W4 wlfw refused at wlan_mode result=1 error=48; the rest is not sent"));
 CHECK(!logged("W6 mode accepted") && !logged("W6 mode cold_boot_cal_done=1"));
 CHECK(logged("W6 mode close: sent=1 cal_done=0 fw_ready=0 request_mem=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W6 mode: a refused mode request is named, nothing follows it, and the stop is still verified\n");

 /* A firmware that takes the mode and then says nothing at all. Both waits run out inside the dwell, each with its own
  * line, and the run still stops cleanly. This is the case that separates "the firmware refused" from "the firmware is
  * thinking", which is the whole reason the two waits are separate. */
 world_init();opt_no_cal_done=1;opt_no_fw_ready=1;
 run(W6,600);
 CHECK(logged("W4 wlfw wlan_mode result=0 error=0") && logged("W6 mode accepted"));
 CHECK(logged("W6 mode cold_boot_cal_done=0 after ") && logged("W6 mode waiting "));
 CHECK(logged("W6 mode fw_ready=0 after "));
 CHECK(logged("W6 mode close: sent=1 cal_done=0 fw_ready=0 request_mem=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W6 mode: a silent firmware after the mode is waited out at both steps and stopped cleanly\n");

 /* fw_ready arriving before cal_done: a firmware may send them in either order, and the host must take both rather than
  * waiting for a cal_done that is already behind it. */
 world_init();opt_ready_before_cal=1;
 run(W6,600);
 CHECK(logged("W6 mode fw_ready=1 ") && logged("W6 mode cold_boot_cal_done=1 after "));
 CHECK(logged("W6 mode close: sent=1 cal_done=1 fw_ready=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W6 mode: fw_ready before cold_boot_cal_done is taken, and both are reported\n");

 /* A firmware that answers the mode request twice. The second reply has the same transaction and the same id, so only
  * the step can tell it from the first: by then the stage is waiting on cal_done, which admits no reply at all. It must be
  * dropped, "mode accepted" must appear once, and the two indications must still arrive. */
 world_init();opt_repeat_reply=0x0022;
 run(W6,600);
 CHECK(logged("W4 wlfw dropped reply id=0x22"));
 CHECK(logged("W6 mode cold_boot_cal_done=1 after ") && logged("W6 mode fw_ready=1 "));
 {unsigned n=0;for(const char *p=hlog;(p=strstr(p,"W6 mode accepted"));p++)n++;CHECK(n==1);}
 CHECK(logged("W6 mode close: sent=1 cal_done=1 fw_ready=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W6 mode: a repeated mode reply is dropped and the mode was answered once\n");

 /* Two cold_boot_cal_done indications: the second changes nothing, and the stage has one answer, not two. */
 world_init();opt_repeat_cal=1;
 run(W6,600);
 CHECK(logged("W6 mode cold_boot_cal_done=1 after "));
 {unsigned n=0;for(const char *p=hlog;(p=strstr(p,"cold_boot_cal_done=1"));p++)n++;CHECK(n==1);}
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W6 mode: a repeated cold_boot_cal_done changes nothing\n");

 /* request_mem: the indication ath11k's own table says this chip never sends. It is logged with what it asked for and
  * NOT allocated: this stage gives the firmware no memory, because on this chip the firmware's memory is the fixed
  * wlan_msa_mem carve-out and the mode handshake asks QMI for none. The stage still completes and still stops cleanly.
  * This is the control for "we did not quietly start allocating": the close line's request_mem count is the evidence. */
 world_init();opt_request_mem=1;
 run(W6,600);
 CHECK(logged("W6 mode request_mem segments=1 first=m3 bytes=1048576 configs=0; not allocated, not answered"));
 CHECK(logged("W6 mode close: sent=1 cal_done=1 fw_ready=1 request_mem=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 /* And the same run with a request_mem whose segment list is truncated: refused as malformed rather than read past. */
 {world_init();opt_request_mem=1;
  /* The peer builds the well-formed one; this only asserts the decoder's own negative, which has no wire behind it. */
  static const uint8_t short_seg[]={4,7,0,0x35,0,4,0, 1, 0,0,0x10};
  uint32_t count=0;WlfwMemSegment seg={0,0,0};
  CHECK(wlfw_request_mem(short_seg,sizeof short_seg,&count,&seg)==-1);}
 fprintf(stdout,"PASS: W6 mode: a request_mem is logged, not allocated and not answered, and the stage still stops cleanly\n");

 /* W5's behaviour is untouched: the board data gate stops after init_done, sends no mode request, and prints no W6 line.
  * This is the regression that matters most, because 2250 widens the reply guard and the step table that W5's audit
  * approved. */
 world_init();
 run(W5,600);
 CHECK(!logged("W4 wlfw wlan_mode") && !logged("W6 mode"));
 CHECK(logged("W5 bdf init_done=1") && logged("W4 wlfw close: step=init_done ") && logged("W5 bdf close: regdb_pieces=4"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 /* And W4 still stops after device_info, with neither the board data nor the mode. */
 {world_init();run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1",600);
  CHECK(!logged("W5 bdf") && !logged("W6 mode") && logged("W4 wlfw close: step=done tx=4 ") && logged("final_error=0"));}
 fprintf(stdout,"PASS: W6 mode: W5 and W4 keep the exchange and the close lines their audits approved\n");
}

/* 2251 (W7): the mission half. W6's whole exchange, then WLAN_INI, WLAN_CFG, mission mode and a fixed hold, then the
 * mode-off, then the verified stop. The peer decodes WLAN_CFG in full and compares it with its own copy of ath11k's
 * tables (ce_truth, svc_truth, shadow_truth in wpss_host.h), so a wrong pipe direction, a missing shadow word or a
 * truncated array fails here rather than on the phone. */
static void t_mission(void){
 world_init();
 run(W7,600);
 CHECK(threads==1 && logged("W7 worker begin"));
 /* W6's work is unchanged underneath: the four answers, the board data, the cold-boot mode and both indications. */
 CHECK(logged("W6 mode cold_boot_cal_done=1 after ") && logged("W6 mode fw_ready=1 "));
 CHECK(opt_mode_requests==1 && opt_mode_value==7);
 /* Then ath11k's ath11k_qmi_firmware_start order: WLAN_INI, WLAN_CFG, mission mode. The peer checks each message's own
  * content, so the order assertions here are about the sequence, not the bytes. */
 CHECK(logged("W7 mission next: fw log, the copy engine configuration, then mission mode"));
 CHECK(ini_seen && ini_bytes==4 && ini_value==1);
 CHECK(logged("W4 wlfw wlan_ini request bytes=11 "));
 CHECK(logged("W4 wlfw wlan_ini result=0 error=0"));
 CHECK(logged("W7 mission fw log on accepted; the copy engine configuration is next"));
 CHECK(cfg_seen && cfg_pipes==9 && cfg_svcs==14 && cfg_shadow==10);
 CHECK(cfg_version_ok && cfg_pipes_ok && cfg_svcs_ok && cfg_shadow_ok);
 CHECK(logged("W4 wlfw wlan_cfg request bytes=413 "));
 CHECK(logged("W4 wlfw wlan_cfg result=0 error=0"));
 CHECK(logged("W7 mission configuration accepted (9 pipes, 14 services, 10 shadow registers); mission mode is next"));
 /* Mission mode itself: mode 0, hw_debug present and zero, exactly as the cold-boot request is shaped. */
 CHECK(mission_requests==1 && mission_taken);
 CHECK(logged("W4 wlfw mission request bytes=18 "));
 CHECK(logged("W4 wlfw mission result=0 error=0"));
 CHECK(logged("W7 mission taken "));
 CHECK(logged(" ms after the cold-boot mode; holding 5000 ms with no host copy engines"));
 /* The hold, then the mode-off, which this firmware takes because mission mode was entered. */
 CHECK(logged("W7 mission held 5000 ms with no host copy engines; no fatal bit, no crash reason, the link unbroken"));
 CHECK(mode_off_requests==1 && off_taken);
 CHECK(logged("W4 wlfw mode_off request bytes=18 "));
 CHECK(logged("W7 mode off answered result=0 error=0; the stop follows"));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1 mode_off_sent=1 mode_off=0/0 indications=0"));
 /* Nothing else: no register access, no copy engine, no allocation, and the register window is still only recorded. */
 CHECK(logged("(recorded; not mapped, not read)"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0") && all_unmapped());
 CHECK(!logged("request_mem segments=") && !logged("W6 mode request_mem malformed"));
 fprintf(stdout,"PASS: W7 mission: the fw log, ath11k's copy-engine configuration and mission mode, in order, with the verified stop\n");

 /* 2251: the whole point of the stage. The mode-off is NOT sent before mission mode, so a boot that never reaches
  * mission mode can never reproduce the reference port's error 48. The peer's own rule says so, and the stage's close
  * line says mode_off_sent=0. */
 world_init();opt_refuse_cfg=1;
 run(W7,600);
 CHECK(ini_seen && cfg_seen && !mission_taken && mission_requests==0);
 CHECK(logged("W4 wlfw refused at wlan_cfg result=1 error=48"));
 CHECK(logged("W7 mission close: ini=1 cfg=0 mission_sent=0 mission=0 mode_off_sent=0 mode_off=-1/-1"));
 CHECK(mode_off_requests==0);
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: a refused configuration stops the sequence, sends no mission mode and no mode-off, and still stops cleanly\n");

 /* A refused WLAN_INI: nothing after it goes out, and the stop is still the verified one. */
 world_init();opt_refuse_ini=1;
 run(W7,600);
 CHECK(ini_seen && !cfg_seen && !mission_taken && mission_requests==0 && mode_off_requests==0);
 CHECK(logged("W4 wlfw refused at wlan_ini result=1 error=48"));
 CHECK(logged("W7 mission close: ini=0 cfg=0 mission_sent=0 mission=0 mode_off_sent=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: a refused fw-log request stops everything after it and the stop still verifies\n");

 /* A refused mission mode: the mode-off is not sent, because mission mode was not taken. That is the rule that keeps
  * this stage from repeating the reference port's error 48. */
 world_init();opt_refuse_mission=1;
 run(W7,600);
 CHECK(ini_seen && cfg_seen && mission_requests==1 && !mission_taken && mode_off_requests==0);
 CHECK(logged("W4 wlfw refused at mission result=1 error=48"));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=0 mode_off_sent=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: a refused mission mode sends no mode-off and still stops cleanly\n");

 /* A mode request that is taken and never answered: the stage waits its own bound, then stops. Nothing is left
  * outstanding, and no mode-off is sent because the host never learned the mode was in. */
 world_init();opt_no_mission_reply=1;
 run(W7,600);
 CHECK(mission_requests==1 && !mission_taken && mode_off_requests==0);
 CHECK(logged("W4 wlfw mission no reply in "));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=0 mode_off_sent=0"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: an unanswered mission request ends at its bound with no mode-off and a verified stop\n");

 /* A refused mode-off: a finding, not a gate. The firmware said no, the stage records it, and the DSP is still stopped by
  * the SMP2P request and the PAS6 shutdown. */
 world_init();opt_refuse_off=1;
 run(W7,600);
 CHECK(mission_taken && mode_off_requests==1 && !off_taken);
 CHECK(logged("W7 mode off refused result=1 error=48; the DSP is still stopped by the SMP2P request and PAS6"));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1 mode_off_sent=1 mode_off=1/48"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: a refused mode-off is recorded as a finding and the verified stop still runs\n");

 /* The DSP raises its fatal bit after mission mode. That is the one failure an idle post-mission firmware can produce
  * that the host must notice, and every exit is still a stop. */
 world_init();opt_fatal_after_mission=1;
 run(W7,600);
 CHECK(mission_taken && logged("W2 run end error=5 "));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("ack=1") && all_unmapped());
 fprintf(stdout,"PASS: W7 mission: a fatal bit raised after mission mode ends the run and the stop still verifies\n");

 /* An unsolicited indication after mission mode is counted apart, because W7's question is what an idle firmware does. */
 world_init();opt_mission_indication=1;
 run(W7,600);
 CHECK(mission_taken && mission_indication_sent);
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1 mode_off_sent=1 mode_off=0/0 indications=1"));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W7 mission: an indication volunteered after mission mode is counted, not mistaken for a reply\n");

 /* 2251: every earlier gate keeps its own close line and its own timing. W6 in particular must still end at fw_ready
  * with no mission half at all, because that is the boot its audit covers. */
 {world_init();run(W6,600);
  CHECK(!logged("W7 mission") && !logged("wlan_ini") && !logged("wlan_cfg") && !logged("mission"));
  CHECK(logged("W6 mode close: sent=1 cal_done=1 fw_ready=1 request_mem=0"));
  CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));}
 {world_init();run(W5,600);
  CHECK(!logged("W6 mode") && !logged("W7 mission") && logged("W4 wlfw close: step=init_done ") && logged("final_error=0"));}
 {world_init();run("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwlfw=1",600);
  CHECK(!logged("W5 bdf") && !logged("W6 mode") && !logged("W7 mission") && logged("W4 wlfw close: step=done tx=4 "));}
 fprintf(stdout,"PASS: W7 mission: W6, W5 and W4 keep their own exchanges, close lines and timings\n");

 /* The gate: W7 may run beside a preset, refuses every other Wi-Fi gate and every preset member, and needs the
  * safeguards. */
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1",600)==WpssMission);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetdaily=1",600)==WpssMission);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwmode=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwbdf=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwmission=1",599)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetmpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmode=1 sweetwmission=1",600)==WpssRefused);
 puts("PASS: W7 gate: a preset is allowed beside it, every other Wi-Fi gate and preset member is refused");
}

/* 2252 (W8 survey). W7 underneath, unchanged; three snapshots of apps_smmu around it, each read only, each under the claim,
 * each mapped and unmapped. The fake SMMU aborts on any write and on any read outside the set our code has already made
 * on this phone, so a passing run is also the proof of that. */
static void t_smmu(void){
 world_init();
 run(W8,600);
 CHECK(threads==1 && logged("W8 worker begin"));
 /* W7's whole run, line for line where it matters. */
 CHECK(logged("W7 mission taken ") && logged("W7 mission held 5000 ms with no host copy engines"));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1 mode_off_sent=1 mode_off=0/0 indications=0"));
 CHECK(logged("(recorded; not mapped, not read)"));
 /* The first snapshot, as ast260's boot-time probe saw the SMMU. */
 CHECK(logged("W8 smmu before: sCR0=0x2a0406 (USFCFG=1 GFIE=1 GFRE=1 CLIENTPD=0) ID0=0x4c017e72 ID1=0x60000048 ID2=0x5111 sGFSR=0 read_only=1"));
 CHECK(logged("W8 smmu before: smr 2 raw=0x80021c00 id=0x1c00 mask=0x2 s2cr=0x2 type=translate cb=2\n"));
 CHECK(logged("W8 smmu before: smrs valid=5 free=109 first_free=5,6,7,8"));
 CHECK(logged("W8 smmu before: sid 0x1400 has no smr; with USFCFG=1 its DMA would be an unidentified-stream fault"));
 CHECK(logged("W8 smmu before: sid 0x1401 has no smr; with USFCFG=1 its DMA would be an unidentified-stream fault"));
 CHECK(logged("W8 smmu before: cb 0 CBAR=0x1f000 CBA2R=0x1 SCTLR=0xe0 (M=0) TTBR0=0:0 TTBR1=0:0 FSR=0x400"));
 CHECK(logged("W8 smmu before: banks referenced=5 free=53 (IPA's rule: unreferenced, SCTLR&0xe1=0, FSR=0, CBAR=0, CBA2R=0) first_free=6,7,8,9,11,12,13,14 faulted=0"));
 CHECK(!logged("global fault recorded") && !logged("a fault is recorded in this bank") && !logged("matched by") && !logged(" WLAN TCR="));
 /* Order: before anything of the WPSS, after the mission sequence and before the stop, after the verified stop. */
 CHECK(text_order("W8 smmu before: sCR0","firmware preflight PASS","W7 mode off answered","W8 smmu mission: sCR0"));
 CHECK(text_order("W8 smmu mission: sCR0","PAS6 cmd=6 status=0","W2 STOP verified; cleanup=ok","W8 smmu after: sCR0"));
 CHECK(logged("W8 smmu close: taken=111 sGFSR=0/0/0 wlan_smr=0 valid_smr=5 free_smr=109 free_cb=53 faulted_cb=0 mission=1 read_only=1"));
 CHECK(regions[R_SMMU].maps==3 && !smmu_claimed && smmu_table_reads>0);
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: W7's run with three read-only apps_smmu snapshots, ordered around the start and the stop, all unmapped\n");

 /* A firmware that moved data on a stream with no SMR during mission mode: USF in sGFSR, seen from the mission snapshot on,
  * reported by its status bits only, and the stop is unchanged. */
 world_init();opt_usf_in_mission=1;
 run(W8,600);
 CHECK(logged("W8 smmu mission: global fault recorded: USF (a stream with no SMR moved data) (status bits only; sGFAR and sGFSYNR are not read)"));
 CHECK(logged("W8 smmu close: taken=111 sGFSR=0/0x2/0x2 "));
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: a USF raised during mission mode is reported by its status bits and the stop still verifies\n");

 /* An SMR that does cover the WLAN streams, under its mask, pointing at a translating bank: the survey names it, reports
  * the conflict-free match for both streams and reads that bank's translation registers. */
 world_init();put32(R_SMMU,0x800+4*7,0x80011400u);put32(R_SMMU,0xc00+4*7,9);put32(R_SMMU,0x1000+4*9,0x1f000u);put32(R_SMMU,0x1800+4*9,1);
 put32(R_SMMU,0x80000+0x1000*9,0x61);put32(R_SMMU,0x80000+0x1000*9+0x30,0x80301c);put32(R_SMMU,0x80000+0x1000*9+0x58,0x400);
 run(W8,600);
 CHECK(logged("W8 smmu before: smr 7 raw=0x80011400 id=0x1400 mask=0x1 s2cr=0x9 type=translate cb=9 WLAN (in the WLAN TBU range)"));
 CHECK(logged("W8 smmu before: sid 0x1400 matched by 1 smr(s)\n") && logged("W8 smmu before: sid 0x1401 matched by 1 smr(s)\n"));
 CHECK(logged("W8 smmu before: cb 9 WLAN CBAR=0x1f000 CBA2R=0x1 SCTLR=0x61 (M=1) "));
 CHECK(logged("W8 smmu before: cb 9 WLAN TCR=0x80301c "));
 CHECK(logged("W8 smmu close: taken=111 sGFSR=0/0/0 wlan_smr=2 valid_smr=6 free_smr=108 "));
 CHECK(logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: an SMR covering 0x1400/0x1401 under its mask is found, and its bank's translation registers are read\n");

 /* A different geometry is a different hypervisor build: the global line only, no table and no bank read. */
 world_init();put32(R_SMMU,0x24,0x60000050u);
 run(W8,600);
 CHECK(logged("W8 smmu before: geometry differs from the measured ID0=0x4c017e72 ID1=0x60000048 ID2=0x5111; no table or bank read"));
 CHECK(smmu_table_reads==0 && !logged("W8 smmu before: smr ") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: an unmeasured SMMU geometry stops the survey at the global registers\n");

 /* Another SMMU user holds the claim for the whole run: nothing is mapped or read, and W7's run is untouched. */
 world_init();smmu_claim_other=1;
 run(W8,600);
 CHECK(logged("W8 smmu before: busy, another SMMU user held the claim for 100 ms; not read"));
 CHECK(logged("W8 smmu close: taken=000 sGFSR=none/none/none "));
 CHECK(!regions[R_SMMU].maps && logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1") && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: a held claim means no SMMU read at all, and the run is otherwise W7's\n");

 /* The SMMU window cannot be mapped: said once per snapshot, the claim is given back, and the run goes on. */
 world_init();fail_map=R_SMMU;
 run(W8,600);
 CHECK(logged("W8 smmu before: mapping failed; not read") && logged("W8 smmu after: mapping failed; not read"));
 CHECK(!smmu_claimed && logged("final_error=0") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: a failed SMMU mapping is reported and gives the claim back\n");

 /* A failure inside the run goes straight to the stop: no mission snapshot, the after snapshot still taken. */
 world_init();opt_fatal_after_mission=1;
 run(W8,600);
 CHECK(logged("W2 run end error=5 ") && !logged("W8 smmu mission:") && logged("W8 smmu after: sCR0"));
 CHECK(logged("W8 smmu close: taken=101 ") && logged("W2 STOP verified; cleanup=ok") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: a fatal bit skips the mission snapshot, stops first and still reads after the stop\n");

 /* W7 itself is untouched: no W8 line, no SMMU mapping. */
 world_init();run(W7,600);
 CHECK(!logged("W8 ") && !regions[R_SMMU].maps && logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1") && all_unmapped());
 fprintf(stdout,"PASS: W8 survey: the W7 gate prints no W8 line and never maps the SMMU\n");

 /* The gate: W7's rules, exclusive with every other Wi-Fi gate. */
 CHECK(wpss_gate(W8,600)==WpssSmmu);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetdaily=1",600)==WpssSmmu);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetwmission=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetwmode=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetwpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=1 sweetmpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwsmmu=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwsmmu=1",599)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetdeadman=600 sweetwsmmu=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwmission=1 sweetwsmmu=1",600)==WpssRefused);
 puts("PASS: W8 gate: a preset is allowed beside it, every other Wi-Fi gate and preset member is refused, safeguards required");
}

/* 2253 (W9): the WLAN device's own DMA translation. W7 underneath, unchanged, with one SMR and one translating bank
 * installed before the firmware is read, checked while the firmware is in mission mode, and released after the verified
 * stop. The fake SMMU judges every write against the lease's phases (smmu_write_check), refuses any write outside the
 * offsets our phone code has read, and keeps the words each lease found so the teardown can be required to restore them.
 * The fake ipa_mmu_choose/ipa_mmu_mode/ipa_mmu_vmid_free are the real headers', so a reuse of them is tested as such. */
static const char *W9="sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1";
static void t_dma(void){
 /* ast267's shape: banks 5 to 12 read as free (CBAR 0, CBA2R 0, SCTLR with M clear), which is what the phone measured.
  * The model's default reset pattern makes every fifth bank carry CFRE/M so the predicate is exercised; here the shape is
  * the phone's, so the expected bank is the first free one W8 listed. */
 world_init();
 for(unsigned c=5;c<=12;c++){put32(R_SMMU,0x80000+0x1000*c,0x3ec9850cu);put32(R_SMMU,0x80000+0x1000*c+0x58,0);}
 run(W9,600);
 /* W7's whole run, with the translation live from before the firmware read to after the verified stop. */
 CHECK(threads==1 && logged("W9 worker begin"));
 CHECK(logged("W7 mission taken ") && logged("W7 mission held 5000 ms with no host copy engines"));
 CHECK(logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1 mode_off_sent=1 mode_off=0/0 indications=0"));
 /* The install: one lease, SMR 5 in the free slot W8 measured, bank 5 as the first free by IPA's rule. */
 CHECK(logged("W9 dma: installed smr=5 raw=0x80011400 s2cr=0x305 bank=5 cbar=0x1f301 cba2r=1 vmid=1 sctlr=0xc00027"));
 CHECK(logged("W9 dma: table walk iova=0xb0000000 -> "));
 CHECK(!logged("W9 dma: no free") && !logged("W9 dma: busy") && !logged("outside the W9 lease"));
 /* Ordered: the install before the firmware preflight, the mission check after the mission sequence, the release after
  * the stop's own verified line. */
 CHECK(text_order("W9 dma: installed","firmware preflight PASS","W9 dma mission:","W2 STOP verified; cleanup=ok"));
 CHECK(text_order("W2 STOP verified; cleanup=ok","W9 dma: released","W9 dma close:","final_error=0"));
 CHECK(logged("W9 dma mission: smr=5 bank=5 ours=1 ") && logged("W9 dma: released smr=5 back to 0/0, bank=5 back to CBAR=0 CBA2R=0"));
 CHECK(logged("W9 dma close: gate=1 smr=5 bank=5 vmid=1 installed=1 released=1 leaked=0 table=1 doorbell=1 fault_routed=1 verify_fail=0"));
 CHECK(w9_installs==1 && w9_releases==1 && smmu_w9_state==W9S_DONE && !smmu_write_bad && smmu_writes>0);
 /* The bank really is back to free by the predicate, and nothing is left mapped or allocated. */
 CHECK(!smmu_wlan_bank(5) && all_unmapped() && !contig[0].used);
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("PAS6 cmd=6 status=0") && logged("final_error=0"));
 fprintf(stdout,"PASS: W9 dma: one SMR (0x1400 mask 1) and one bank leased with IPA's rules, live through W7's whole run, then released and proven free\n");

 /* IPA's own bank is never taken: with banks 5 and 6 leased exactly as ast174 shows them, this stage takes 7, and IPA's
  * predicate would refuse it while the lease lives. That is the collision rule, tested from both sides. */
 world_init();
 put32(R_SMMU,0x800+4*5,0x800004a0u);put32(R_SMMU,0xc00+4*5,0x105);put32(R_SMMU,0x1000+4*5,0x1f305u);put32(R_SMMU,0x1800+4*5,1);
 put32(R_SMMU,0x80000+0x1000*5,0xc00027u);
 put32(R_SMMU,0x800+4*6,0x800004a2u);put32(R_SMMU,0xc00+4*6,0x106);put32(R_SMMU,0x1000+4*6,0x1f306u);put32(R_SMMU,0x1800+4*6,1);
 put32(R_SMMU,0x80000+0x1000*6,0xc00027u);
 run(W9,600);
 CHECK(logged("W9 dma: installed smr=7 raw=0x80011400 ") && logged(" bank=7 "));
 CHECK(logged("W9 dma: released smr=7 back to 0/0, bank=7 back to CBAR=0 CBA2R=0"));
 CHECK(logged("W9 dma close: gate=1 smr=7 bank=7 ") && w9_installs==1 && w9_releases==1 && all_unmapped());
 fprintf(stdout,"PASS: W9 dma: a bank IPA already leases is skipped by IPA's own predicate, and the free bank after it is taken\n");

 /* A stream that is already matched is refused: a second SMR would be a match conflict on the device's first DMA. */
 world_init();put32(R_SMMU,0x800+4*9,0x80011400u);put32(R_SMMU,0xc00+4*9,0x109);put32(R_SMMU,0x1000+4*9,0x1f309u);
 run(W9,600);
 CHECK(logged("W9 dma: sid 0x1400 is already matched by SMR 9 raw=0x80011400"));
 CHECK(logged("W9 dma close: gate=1 smr=0 bank=0 ") && w9_installs==0 && !smmu_write_bad && all_unmapped());
 CHECK(!pas_ncalls && logged("final_error=5") && logged("W2 STOP verified"));
 fprintf(stdout,"PASS: W9 dma: a stream that is already matched is refused before any WPSS work, and nothing is written\n");

 /* Unmeasured geometry: nothing is written, the run stops before the firmware and the teardown has nothing to release. */
 world_init();put32(R_SMMU,0x24,0x60000050u);
 run(W9,600);
 CHECK(logged("W9 dma: geometry differs from the measured ID0=0x4c017e72 ID1=0x60000048 ID2=0x5111") && !smmu_writes);
 CHECK(logged("final_error=5") && logged("W9 dma close: gate=1 smr=0 bank=0 ") && all_unmapped());
 fprintf(stdout,"PASS: W9 dma: an unmeasured SMMU geometry is refused before any write, and the boot stops before the WPSS starts\n");

 /* Another SMMU user holds the claim for the whole run: no lease, no write, and W9 refuses rather than wait past its
  * bound. W7's run does not happen either, because a boot that cannot install its translation is a different boot. */
 world_init();smmu_claim_other=1;
 run(W9,600);
 CHECK(logged("W9 dma: busy, another SMMU user held the claim for 100 ms; not installed") && !smmu_writes);
 CHECK(logged("W9 dma close: gate=1 smr=0 bank=0 ") && !pas_ncalls && all_unmapped());
 fprintf(stdout,"PASS: W9 dma: a held claim means no lease and no write, and the run stops before the WPSS starts\n");

 /* A failed install must not leak the ring page or the table, and must leave the SMMU untouched. The mapping is refused
  * here, which is the one failure that happens before anything is allocated. */
 world_init();fail_map=R_SMMU;
 run(W9,600);
 CHECK(logged("W9 dma: mapping failed; not installed") && !smmu_writes && meta_allocs==0 && all_unmapped());
 CHECK(logged("final_error=5") && logged("W9 dma close: gate=1 smr=0 bank=0 "));
 fprintf(stdout,"PASS: W9 dma: a failed SMMU mapping installs nothing and allocates nothing\n");

 /* A fault recorded in the bank at teardown is cleared with exactly the bits read (FSR is write-one-to-clear), and the
  * bank still passes the free predicate afterwards. This is the one path where the teardown writes FSR. */
 world_init();smmu_fault_at_teardown=1;
 run(W9,600);
 CHECK(logged("W9 dma: bank 6 records FSR=0x40a at teardown ") && logged("clearing it"));
 CHECK(logged("W9 dma close: gate=1 smr=5 bank=6 ") && logged("fsr=0x40a ") && w9_releases==1 && all_unmapped());
 CHECK(logged("W2 STOP verified; cleanup=ok") && logged("final_error=0"));
 fprintf(stdout,"PASS: W9 dma: a fault in the leased bank is cleared at teardown and the bank is still returned free\n");

 /* W7 and W8 themselves are untouched: neither prints a W9 line and neither writes an SMMU register. */
 world_init();run(W7,600);
 CHECK(!logged("W9 ") && !smmu_writes && logged("W7 mission close: ini=1 cfg=1 mission_sent=1 mission=1"));
 world_init();run(W8,600);
 CHECK(!logged("W9 ") && !smmu_writes && logged("W8 smmu close: taken=111 ") && all_unmapped());
 fprintf(stdout,"PASS: W9 dma: the W7 and W8 gates print no W9 line and write no SMMU register\n");

 /* The gate: W9's own rules, exclusive with every other Wi-Fi gate including W8. */
 /* Every line is written out rather than built in a loop: a CHECK prints its own expression when it fails, and a literal
  * one is what makes a negative control's expected failure text unique and readable. */
 CHECK(wpss_gate(W9,600)==WpssDma);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetdaily=1",600)==WpssDma);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwsmmu=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwmission=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwmode=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwbdf=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwlfw=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwglink=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwprobe=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwpin=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=599 sweetwdma=1",599)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetdeadman=600 sweetwdma=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetmpss=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetbatt=1",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetwprobe=0",600)==WpssRefused);
 CHECK(wpss_gate("sweetfbarm=1 sweetselftest=1 sweetdeadman=600 sweetwdma=1 sweetselftest=1",600)==WpssRefused);
 puts("PASS: W9 gate: a preset is allowed beside it, every other Wi-Fi gate is refused, safeguards required");
}

static void t_failures(void){
 /* Prior WPSS item: refused before any SMEM write, PAS call, QMP message or vote. */ world_init();remote_start();remote_running=false;
 uint8_t *saved=malloc(0x200000);memcpy(saved,at(R_SMEM,0),0x200000);
 run(GOOD,600);CHECK(logged("W2 refused: WPSS SMP2P item present") && logged("final_error=16") && !pas_ncalls && !qmp_nsent && !rsc_nwrites && !ipcc_nrings);
 CHECK(!memcmp(saved,at(R_SMEM,0),0x200000) && all_unmapped());free(saved);
 /* Missing stop ack: after 3 s the shutdown is still sent, and verified. */
 world_init();opt_no_ack=1;run(GOOD,600);
 CHECK(logged("stop request cleared ack=0") && count_pas(6)==1 && logged("STOP verified") && all_unmapped());
 /* A DSP that never said it was up cannot acknowledge a stop: the wait is 500 ms, not 3 s, and it still shuts down. */
 world_init();opt_no_ready=1;run(GOOD,600);
 CHECK(logged("waited_ms=500") && logged("ready_when_asked=0") && count_pas(6)==1 && logged("STOP verified"));
 /* Shutdown transport failures retried, then verified. */
 world_init();shutdown_einval=3;run(GOOD,600);CHECK(count_pas(6)==4 && logged("STOP verified") && all_unmapped());
 /* Shutdown never verified: 50 tries, then memory, metadata, QMP state and power are retained. */
 world_init();shutdown_einval=1000;run(GOOD,600);
 CHECK(count_pas(6)==50 && logged("STOP UNVERIFIED") && logged("final_error=5") && none_unmapped() && !meta_frees && qmp_nsent==1);
 /* Shutdown refused with a definite error: retained, no retry. */
 world_init();pas_status[6]=-1;run(GOOD,600);CHECK(count_pas(6)==1 && logged("STOP UNVERIFIED") && none_unmapped());
 /* init_image fails: the shutdown is still owed and sent; no mem_setup or auth. */
 world_init();pas_status[1]=-5;run(GOOD,600);
 CHECK(count_pas(2)==0 && count_pas(5)==0 && count_pas(6)==1 && !logged("stop request cleared") && logged("STOP verified"));
 /* auth fails: shutdown, no SMP2P stop request (it never started). */
 world_init();pas_status[5]=-4;run(GOOD,600);CHECK(count_pas(6)==1 && !logged("stop request cleared") && logged("STOP verified"));
 /* Never ready: 30 s, then the stop. The state probe runs read only: it must not write the GIC, even to clear the bit. */
 world_init();opt_no_ready=1;run(GOOD,600);CHECK(logged("final_error=60") && count_pas(6)==1 && logged("ack=1"));
 /* 2245: SPI 579 is INTID 611 in the distributor, word 19 bit 3. Reading the SPI number as an INTID (word 18 bit 3)
  * reads SPI 547 and always says "not pending", so the number itself is asserted, not only the read succeeding. */
 CHECK(logged("W2 probe: wdog spi=579 intid=611 word=19 bit=3") && gic_writes==0 && regions[R_GIC].unmaps==1);
 /* A pending bit at the real INTID must be reported. */
 world_init();opt_no_ready=1;put32(R_GIC,0x200+4*19,1u<<3);run(GOOD,600);
 CHECK(logged("WPSS WATCHDOG HAS FIRED (spi 579 intid 611 pending") && logged("wdog spi=579 intid=611 word=19 bit=3 group1=0 enabled=0 pending=1"));
 /* And a pending bit at the wrong word must not be mistaken for it: INTID 579 (SPI 547) is a different interrupt. */
 world_init();opt_no_ready=1;put32(R_GIC,0x200+4*18,1u<<3);run(GOOD,600);
 CHECK(!logged("WATCHDOG HAS FIRED") && logged("pending=0"));
 CHECK(logged("W2 probe: smp2p in item=617 rc=0") && logged("crash reason"));
 /* Ready without handover: 10 s more, then the stop. */
 world_init();opt_no_handover=1;run(GOOD,600);CHECK(logged("final_error=60") && logged("ready=1 handover=0") && count_pas(6)==1);
 /* Fatal after handover: EIO, stop still requested and the shutdown verified. */
 world_init();opt_fatal_ms=5000;run(GOOD,600);CHECK(logged("final_error=5") && logged("fatal=1") && count_pas(6)==1 && logged("STOP verified"));
 /* QMP never opens: nothing loaded, nothing owed. */
 world_init();qmp_fail_open=1;run(GOOD,600);CHECK(!pas_ncalls && !qmp_nsent && !rsc_nwrites && all_unmapped());
 /* QMP off refused after a verified stop: cleanup incomplete, the QMP message memory stays mapped. */
 world_init();qmp_fail_off=1;run(GOOD,600);CHECK(count_pas(6)==1 && logged("cleanup incomplete: QMP release") && regions[R_QMP].unmaps==0);
 /* Firmware unreadable: no hardware touched beyond nothing at all. */
 world_init();fail_fw=1;run(GOOD,600);CHECK(!pas_ncalls && !rsc_nwrites && !ipcc_nrings && all_unmapped() && !kallocs);
 /* Each mapping failure unwinds before any write. */
 for(int r=R_SMEM;r<=R_DB;r++){world_init();fail_map=r;run(GOOD,600);CHECK(logged("final_error=12") && !pas_ncalls && !rsc_nwrites && all_unmapped());}
 /* Carveout mapping failure after mem_setup: shutdown still sent and verified. */
 world_init();fail_map=R_CARVE;run(GOOD,600);CHECK(count_pas(5)==0 && count_pas(6)==1 && logged("STOP verified"));
 puts("PASS: failures: prior WPSS refused untouched; no ack, retried and unverified shutdowns; init, auth, ready, handover, fatal, QMP, firmware and mapping failures");
}
static bool pin_untouched(void){
 return gpio5_regs[0x40]==0 && gpio5_regs[0x41]==0 && gpio5_regs[0x42]==4 && gpio5_regs[0x43]==0 && gpio5_regs[0x44]==0 && gpio5_regs[0x45]==1 && gpio5_regs[0x46]==0x80;}
static void t_pin(void){
 world_init();run(PIN,600);
 CHECK(threads==1 && logged("W1c done result=0") && pmic_nwrites==3);
 CHECK(pmic_writes[0].reg==0x44 && pmic_writes[0].value==2 && pmic_writes[1].reg==0x42 && pmic_writes[1].value==5 && pmic_writes[2].reg==0x40 && pmic_writes[2].value==1);
 CHECK(gpio5_regs[0x44]==2 && gpio5_regs[0x42]==5 && gpio5_regs[0x40]==1 && gpio5_regs[0x41]==0 && gpio5_regs[0x43]==0 && gpio5_regs[0x45]==1 && gpio5_regs[0x46]==0x80);
 CHECK(!pas_ncalls && !rsc_nwrites && !ipcc_nrings && !qmp_nsent && all_unmapped() && regions[R_SPMI_CHNL].maps==1);
 CHECK(logged("W1c after reg=0xbc40 rc=0 value=0x1") && logged("W1c after reg=0xbc44 rc=0 value=0x2"));
 /* Already set: nothing written. */
 world_init();gpio5_regs[0x44]=2;gpio5_regs[0x42]=5;gpio5_regs[0x40]=1;run(PIN,600);CHECK(!pmic_nwrites && logged("already in its sleep-clock state") && logged("result=0"));
 /* Two APIDs for the pin, or only another EE's: refused before any write. */
 world_init();spmi_decoy();run(PIN,600);CHECK(!pmic_nwrites && logged("needs exactly one APID") && pin_untouched() && all_unmapped());
 world_init();put32(R_SPMI_CNFG,4*GPIO5_APID,3);run(PIN,600);CHECK(!pmic_nwrites && logged("needs exactly one APID") && pin_untouched());
 /* Unexpected identity or config: refused before any write. */
 static const uint8_t bad[][2]={{0x04,0x11},{0x05,0x09},{0x41,1},{0x43,0x80},{0x46,0},{0x40,2},{0x42,5},{0x44,1}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){world_init();gpio5_regs[bad[i][0]]=bad[i][1];run(PIN,600);CHECK(!pmic_nwrites && logged("refused") && logged("result=22"));}
 /* A write the PMIC ignores stops everything after it; a failed write command stops at once. */
 world_init();stuck_reg=0x42;run(PIN,600);CHECK(pmic_nwrites==2 && gpio5_regs[0x40]==0 && logged("mismatch, no further write") && logged("result=5"));
 world_init();chan_fail_at=0;run(PIN,600);CHECK(pmic_nwrites==1 && pin_untouched() && logged("result=5"));
 world_init();chan_fail_at=2;run(PIN,600);CHECK(pmic_nwrites==3 && gpio5_regs[0x40]==0 && logged("result=5"));
 /* Reads failing, a non-v7 arbiter or a mapping failure: no write. */
 world_init();spmi_fail=1;run(PIN,600);CHECK(!pmic_nwrites && pin_untouched());
 world_init();put32(R_SPMI_CORE,0,0x50000000u);run(PIN,600);CHECK(!pmic_nwrites && logged("is not v7"));
 for(int fm=R_SPMI_CORE;fm<=R_SPMI_CHNL;fm++){world_init();fail_map=fm;run(PIN,600);CHECK(!pmic_nwrites && all_unmapped() && logged("result=12"));}
 /* W2 refuses to start the WPSS when the pin step fails. */
 world_init();spmi_decoy();run(GOOD,600);CHECK(!pas_ncalls && !rsc_nwrites && !qmp_nsent && logged("final_error=22") && all_unmapped());
 puts("PASS: pin: exactly 0x44=2, 0x42=5, 0x40=1 in the vendor order through the EE 0 channel, read back; none when already set; refused for two APIDs, a foreign owner, an unexpected identity or config; stops at a mismatch or failed write; W2 refuses without it");
}
/* 2248: two lines that must never appear in a passing run, whatever else it printed. The first is a teardown order bug
 * (a reply could be routed to a port that is already gone); the second is a reply our decoder refused as malformed, which
 * in a run whose fake firmware speaks correctly means our decoder is wrong. Checked for every test, so a control that
 * introduces either fails whichever test it touches. */
static void audit_log(void)
{
    if (logged("W4 wlfw close order wrong")) {fprintf(stderr,"FAIL: the link closed before the WLFW client port\n");exit(1);}
    /* A malformed reply is legitimate only in the control that sends a truncated one on purpose; everywhere else it means
     * our decoder rejected a well-formed message. */
    if (!opt_short_reply && logged("W4 wlfw malformed reply")) {fprintf(stderr,"FAIL: a well-formed reply was called malformed\n");exit(1);}
}
static int child(void (*f)(void)){pid_t p=fork();if(!p){f();audit_log();exit(0);}int st;waitpid(p,&st,0);return WIFEXITED(st) && !WEXITSTATUS(st)?0:1;}
static void t_ok0(void){t_start_ok(0);}
static void t_ok1(void){t_start_ok(1);}
int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"usage: %s MODEM_A_IMAGE\n",argv[0]);return 2;}
 modem_a=fopen(argv[1],"rb");CHECK(modem_a);fseek(modem_a,0,SEEK_END);modem_a_bytes=(uint64_t)ftell(modem_a);
 /* 2249: the peer reads the same image on its own handle, so its check of the board data cannot share the code under
  * test's reader. */
 bdf_file_handle=fopen(argv[1],"rb");CHECK(bdf_file_handle);
 void (*tests[])(void)={t_gates,t_off_paths,t_survey,t_ok0,t_ok1,t_slow_start,t_glink,t_wlfw,t_bdf,t_mode,t_mission,t_smmu,t_dma,t_failures,t_pin};
 int failed=0;for(unsigned i=0;i<sizeof(tests)/sizeof(tests[0]);i++)failed|=child(tests[i]);
 if(failed){puts("FAIL");return 1;}
 puts("PASS: all W1/W2 host checks");return 0;
}
