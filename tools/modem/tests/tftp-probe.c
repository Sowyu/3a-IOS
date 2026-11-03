#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "ModemTftpProbe.h"
int main(void)
{
    const char *args="sweetmtftpprobe=1 sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1 sweetmstop=1 sweetmho=1 sweetfbarm=1 sweetselftest=1 sweetdeadman=600";
    assert(modem_tftp_probe_allowed(args));
    assert(!modem_tftp_probe_allowed(""));
    char omitted[512];
    for(const char *start=args;*start;) {
        const char *end=strchr(start,' ');if(!end)end=start+strlen(start);
        unsigned before=(unsigned)(start-args);
        memcpy(omitted,args,before);
        snprintf(omitted+before,sizeof(omitted)-before,"%s",*end?end+1:end);
        assert(!modem_tftp_probe_allowed(omitted));
        if(!*end)break;
        start=end+1;
    }

    const char *bad[]={"sweetdata=1","sweetqmiraw=1","sweetipa=1","sweetipafw=1","sweetipamap=1","sweetmsession=1","sweetmtftpprobe=1","sweetdeadman=900"};
    char gates[512];
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        snprintf(gates,sizeof(gates),"%s %s",args,bad[i]);assert(!modem_tftp_probe_allowed(gates));
    }
    static const uint8_t rrq[]="\0\1/readonly/firmware/image/modem_pr/test.mbn\0octet\0blksize\0" "7680\0";
    uint8_t out[32];ModemTftpProbe p;
    memset(out,0xa5,sizeof(out));
    unsigned n=modem_tftp_probe(rrq,sizeof(rrq)-1,&p,out,15);
    assert(n==15 && p.opcode==1 && p.valid && !strcmp(p.path,"/readonly/firmware/image/modem_pr/test.mbn"));
    assert(out[0]==0 && out[1]==5 && out[3]==2 && out[14]==0 && out[15]==0xa5);
    uint8_t req[600];memcpy(req,rrq,sizeof(rrq));req[1]=2;
    assert(modem_tftp_probe(req,sizeof(rrq)-1,&p,out,sizeof(out))==15 && p.opcode==2 && p.valid && out[3]==2);
    for(unsigned op=0;op<256;op++)if(op!=1 && op!=2) {
        req[0]=0;req[1]=op;assert(!modem_tftp_probe(req,2,&p,out,sizeof(out)));
    }
    req[0]=0;req[1]=1;
    for(unsigned cap=0;cap<15;cap++) {
        memset(out,0xa5,sizeof(out));assert(!modem_tftp_probe(rrq,sizeof(rrq)-1,&p,out,cap));
        for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xa5);
    }
    for(unsigned len=0;len<sizeof(rrq)-1;len++) {
        n=modem_tftp_probe(rrq,len,&p,out,sizeof(out));
        assert(n==0 || (n==15 && out[1]==5));
        /* A complete octet request without options is also valid. */
        if(p.valid)assert(rrq[len-1]==0);
    }
    static const uint8_t malformed[]="\0\1name\0octet\0key\0";
    assert(modem_tftp_probe(malformed,sizeof(malformed)-1,&p,out,sizeof(out))==15 && !p.valid && out[3]==4);
    uint32_t seed=1;
    for(unsigned len=0;len<=sizeof(req);len++) {
        for(unsigned i=0;i<len;i++){seed=seed*1664525u+1013904223u;req[i]=(uint8_t)(seed>>24);}
        if(len>=2){req[0]=0;req[1]=(len&1)+1;}
        n=modem_tftp_probe(req,len,&p,out,sizeof(out));assert(n<=15 && p.path[127]==0);
        if(n)assert(out[1]==5 && out[3]>=2 && out[3]<=4);
    }
    puts("PASS bounded TFTP request parsing; RRQ/WRQ always denied; no data or storage interface");
    return 0;
}
