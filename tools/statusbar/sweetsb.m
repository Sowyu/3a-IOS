/* Fixed-canvas Split828 for UIKit apps and SystemStatusUI. No image or screen transforms. */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <dispatch/dispatch.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <limits.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "sweetsb_guard.h"

#define FAMILIES 2
#define CHAIN 4
// ast35 preview: clock margin ~38.5 pt, battery nub margin ~21 pt.
// Move the trailing region as a unit, retaining room for cellular and Wi-Fi.
/* Owner's pick: clock and battery both about 25 pt from their corners. Measured before: clock 38.5, battery with nub 21 (ast35, +-1.2).
   Tune from a captured frame. Clock: 1 pt of inset moves it about 1 pt (ast43 measured 25.7). */
static const double BATTERY_INWARD_SHIFT_PT = 17;  /* measured, green fill to screen edge: inset 41.5 gives 7.0, 47.5 gives 12.8, so about 1 pt per pt; 60.5 should give about 26 */
static const double CLOCK_OUTWARD_SHIFT_PT = 12.5;
static const double VERTICAL_SHIFT_PT = 2;  /* owner photo at 6: items 4.7 pt below the camera centre; the camera centre measures about 27.7 pt from the top */
static const char *const prefix[]={"_UIStatusBarVisualProvider_","STUIStatusBarVisualProvider_"};
static const char *const family[]={"UIKit","STUI"};
static const char *const chain[]={"iOS","Phone","Split","DynamicSplit"};
static Class targets[FAMILIES];
static IMP originals[FAMILIES*CHAIN], styleOriginal[FAMILIES], trailingOriginal[FAMILIES], leadingOriginal[FAMILIES];
static IMP baselineOriginal[FAMILIES];
static BOOL installed[FAMILIES], missingReported[FAMILIES], rejected[FAMILIES];
static atomic_bool selected[FAMILIES], notchReported[FAMILIES], screenReported[FAMILIES], healthyScheduled;
static atomic_bool trailingReported[FAMILIES], leadingReported[FAMILIES];
static atomic_bool verticalReported[FAMILIES];
static atomic_int styleCount[FAMILIES];
static atomic_ullong styleLast[FAMILIES]={ULLONG_MAX,ULLONG_MAX};
static NSString *statePath;
static char boot[64];
static BOOL save(int starts);

/* App stderr may go nowhere. Kernel patch 0048 prints /dev/null writes of the processes named in boot-arg sweetnull
   as "|name| text", so a process whose stderr is not /dev/null also writes there. */
static int teeFd=-1;
/* With /private/var/tmp/sweetsb-probe.on present at process start, every line also goes to /private/var/tmp/sweetsb-real.log
   as "[exe pid] text", so a run can read any app without naming it in sweetnull. Off by default. */
static int logFd=-1;
static char logWho[40];
static void openTee(void) {
    struct stat e,n;
    int fd=open("/dev/null",O_WRONLY|O_CLOEXEC);
    if(fd<0)return;
    if(!fstat(STDERR_FILENO,&e) && !fstat(fd,&n) && S_ISCHR(e.st_mode) && e.st_rdev==n.st_rdev)close(fd);
    else teeFd=fd;
}
/* Raw diagnostics avoid Objective-C messaging during installation. */
static void note(const char *fmt, ...) {
    char line[512];
    int n=snprintf(line,sizeof(line),"sweetsb-real: ");
    va_list ap; va_start(ap,fmt); vsnprintf(line+n,sizeof(line)-n,fmt,ap); va_end(ap);
    size_t len=strnlen(line,sizeof(line)-2);line[len++]='\n';
    (void)write(STDERR_FILENO,line,len);
    if(teeFd>=0)(void)write(teeFd,line,len);
    if(logFd>=0) {
        char tagged[600];
        int m=snprintf(tagged,sizeof(tagged),"[%s %d] %.*s",logWho,getpid(),(int)len,line);
        if(m>0)(void)write(logFd,tagged,(size_t)m<sizeof(tagged)?(size_t)m:sizeof(tagged)-1);
    }
}
static BOOL signature(Method m,const char *ret,unsigned n,const char *last) {
    if (!m || method_getNumberOfArguments(m)!=n) return NO;
    char t[256];method_getReturnType(m,t,sizeof(t));if(strcmp(t,ret))return NO;
    for(unsigned i=0;i<n;i++) {
        method_getArgumentType(m,i,t,sizeof(t));
        if(strcmp(t,i==1?":":(i==n-1&&last?last:"@")))return NO;
    }
    return YES;
}
static void badMethod(int f,const char *step,Method m) {
    note("%s failed %s: %s",family[f],step,m?method_getTypeEncoding(m):"method absent");
}
static BOOL canvas(int f,id screen) {
    Class c=objc_getClass("UIScreen");
    BOOL log=!atomic_exchange(&screenReported[f],true);
    if(!c||![screen isKindOfClass:c]) { if(log)note("%s failed screen class: %s",family[f],screen?object_getClassName(screen):"nil"); return NO; }
    const char *names[]={"nativeBounds","scale","nativeScale","_isEmbeddedScreen"};
    const char *types[]={@encode(CGRect),"d","d",@encode(BOOL)};
    for(int i=0;i<4;i++) {
        Method m=class_getInstanceMethod(object_getClass(screen),sel_registerName(names[i]));
        if(!signature(m,types[i],2,NULL)) {if(log)badMethod(f,names[i],m);return NO;}
    }
    CGRect r=((CGRect(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeBounds"));
    double s=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("scale"));
    double ns=((double(*)(id,SEL))objc_msgSend)(screen,sel_registerName("nativeScale"));
    BOOL embedded=((BOOL(*)(id,SEL))objc_msgSend)(screen,sel_registerName("_isEmbeddedScreen"));
    BOOL fits=r.size.width==840&&r.size.height==1860&&s==2&&ns==2&&embedded;
    if(log)note("%s screen %s native=%gx%g scale=%g nativeScale=%g embedded=%d fits=%d",family[f],object_getClassName(screen),r.size.width,r.size.height,s,ns,embedded,fits);
    return fits;
}
/* SpringBoard runs for the whole boot, so 60 s of main-queue time is a fair healthy mark. An app is often suspended or killed
   sooner, and with 60 s every reopen after that read as a crash and ran stock (the gap the owner still saw). Apps get 3 s. */
static int64_t healthySeconds=60;
static dispatch_queue_t healthyQueue;   // main queue in SpringBoard; a background queue in apps
static void scheduleHealthy(void) {
    if(!atomic_exchange(&healthyScheduled,true))
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,healthySeconds*NSEC_PER_SEC),healthyQueue?healthyQueue:dispatch_get_main_queue(),^{save(0);});
}
#include "sweetsb_safearea.h"
#include "sweetsb_home.h"
static id decide(int n,id self,SEL cmd,id screen,id info) {
    int f=n/CHAIN;
    if(targets[f]&&canvas(f,screen)) {
        if(!atomic_exchange(&selected[f],true))note("%s choose Split828; notch=96.5,0,227,32",family[f]);
        scheduleHealthy();
        return targets[f];
    }
    return ((id(*)(id,SEL,id,id))originals[n])(self,cmd,screen,info);
}
#define CHOOSE(n) static id choose##n(id s,SEL c,id sc,id info){return decide(n,s,c,sc,info);}
CHOOSE(0) CHOOSE(1) CHOOSE(2) CHOOSE(3) CHOOSE(4) CHOOSE(5) CHOOSE(6) CHOOSE(7)
static const IMP chooses[]={(IMP)choose0,(IMP)choose1,(IMP)choose2,(IMP)choose3,(IMP)choose4,(IMP)choose5,(IMP)choose6,(IMP)choose7};
static CGRect notchFor(int f) {
    if(!atomic_exchange(&notchReported[f],true))note("%s notch override used",family[f]);
    return CGRectMake(96.5,0,227,32);
}
static CGRect notch0(id s,SEL c){return notchFor(0);}
static CGRect notch1(id s,SEL c){return notchFor(1);}
static double trailingFor(int f,id s,SEL c) {
    double original=((double(*)(id,SEL))trailingOriginal[f])(s,c);
    double adjusted=original+BATTERY_INWARD_SHIFT_PT;
    if(!atomic_exchange(&trailingReported[f],true))
        note("%s trailing inset used: %g -> %g; inward=%gpt",family[f],original,adjusted,BATTERY_INWARD_SHIFT_PT);
    return adjusted;
}
static double trailing0(id s,SEL c){return trailingFor(0,s,c);}
static double trailing1(id s,SEL c){return trailingFor(1,s,c);}
static double leadingFor(int f,id s,SEL c) {
    double original=((double(*)(id,SEL))leadingOriginal[f])(s,c);
    double adjusted=original-CLOCK_OUTWARD_SHIFT_PT;
    if(!atomic_exchange(&leadingReported[f],true))
        note("%s leading inset used: %g -> %g; outward=%gpt",family[f],original,adjusted,CLOCK_OUTWARD_SHIFT_PT);
    return adjusted;
}
static double leading0(id s,SEL c){return leadingFor(0,s,c);}
static double leading1(id s,SEL c){return leadingFor(1,s,c);}
// FixedSplit anchors each main region's bottom at guide.bottom - this inset.
// Alignment 5 puts item baselines there; subtracting moves both rows down.
static double baselineFor(int f,id s,SEL c) {
    double original=((double(*)(id,SEL))baselineOriginal[f])(s,c);
    double adjusted=original-VERTICAL_SHIFT_PT;
    if(!atomic_exchange(&verticalReported[f],true))
        note("%s baseline inset used: %g -> %g; down=%gpt",family[f],original,adjusted,VERTICAL_SHIFT_PT);
    return adjusted;
}
static double baseline0(id s,SEL c){return baselineFor(0,s,c);}
static double baseline1(id s,SEL c){return baselineFor(1,s,c);}
/* Observe the style decision, never force white/black globally. */
static NSInteger effective(int f,id s,SEL c,NSInteger style) {
    NSInteger result=((NSInteger(*)(id,SEL,NSInteger))styleOriginal[f])(s,c,style);
    uint64_t pair=((uint64_t)(uint32_t)style<<32)|(uint32_t)result;
    if(atomic_exchange(&styleLast[f],pair)!=pair && atomic_fetch_add(&styleCount[f],1)<32)note("%s style requested=%ld resolved=%ld",family[f],(long)style,(long)result);
    return result;
}
static NSInteger style0(id s,SEL c,NSInteger v){return effective(0,s,c,v);}
static NSInteger style1(id s,SEL c,NSInteger v){return effective(1,s,c,v);}
static void installFamily(int f) {
    if(installed[f]||rejected[f])return;
    char name[96];snprintf(name,sizeof(name),"%sSplit828",prefix[f]);
    Class split=objc_lookUpClass(name);
    if(!split) {if(!missingReported[f]){missingReported[f]=YES;note("%s waiting: %s absent",family[f],name);}return;}
    Method nr=class_getInstanceMethod(split,sel_registerName("notchRect"));
    if(!signature(nr,@encode(CGRect),2,NULL)){badMethod(f,"notchRect",nr);rejected[f]=YES;return;}
    SEL trailing=sel_registerName("trailingCenteringEdgeInset");
    Method tr=class_getInstanceMethod(split,trailing);
    if(!signature(tr,@encode(double),2,NULL)){badMethod(f,"trailingCenteringEdgeInset",tr);rejected[f]=YES;return;}
    SEL leading=sel_registerName("leadingCenteringEdgeInset");
    Method ld=class_getInstanceMethod(split,leading);
    if(!signature(ld,@encode(double),2,NULL)){badMethod(f,"leadingCenteringEdgeInset",ld);rejected[f]=YES;return;}
    SEL baseline=sel_registerName("baselineBottomInset");
    Method bl=class_getInstanceMethod(split,baseline);
    if(!signature(bl,@encode(double),2,NULL)){badMethod(f,"baselineBottomInset",bl);rejected[f]=YES;return;}
    SEL sel=sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:");
    Method ms[CHAIN];
    for(int i=0;i<CHAIN;i++) {
        snprintf(name,sizeof(name),"%s%s",prefix[f],chain[i]);
        Class c=objc_lookUpClass(name);
        if(!c){note("%s waiting: %s absent",family[f],name);return;}
        ms[i]=class_getClassMethod(c,sel);
        if(!signature(ms[i],"#",4,NULL)&&!signature(ms[i],"@",4,NULL)) {
            badMethod(f,name,ms[i]);rejected[f]=YES;return;
        }
    }
    note("%s ABI verified: selector #(@,@), notch CGRect(), trailing double()",family[f]);
    snprintf(name,sizeof(name),"SweetReal%dSplit828",f);
    Class t=objc_allocateClassPair(split,name,0);
    if(!t){note("%s failed allocate subclass",family[f]);rejected[f]=YES;return;}
    if(!class_addMethod(t,sel_registerName("notchRect"),f?(IMP)notch1:(IMP)notch0,method_getTypeEncoding(nr))) {
        note("%s failed add notchRect",family[f]);rejected[f]=YES;return;
    }
    trailingOriginal[f]=method_getImplementation(tr);
    if(!class_addMethod(t,trailing,f?(IMP)trailing1:(IMP)trailing0,method_getTypeEncoding(tr))) {
        note("%s failed add trailingCenteringEdgeInset",family[f]);rejected[f]=YES;return;
    }
    leadingOriginal[f]=method_getImplementation(ld);
    if(!class_addMethod(t,leading,f?(IMP)leading1:(IMP)leading0,method_getTypeEncoding(ld))) {
        note("%s failed add leadingCenteringEdgeInset",family[f]);rejected[f]=YES;return;
    }
    baselineOriginal[f]=method_getImplementation(bl);
    if(!class_addMethod(t,baseline,f?(IMP)baseline1:(IMP)baseline0,method_getTypeEncoding(bl))) {
        note("%s failed add baselineBottomInset",family[f]);rejected[f]=YES;return;
    }
    objc_registerClassPair(t);targets[f]=t;
    for(int i=0;i<CHAIN;i++) {
        BOOL shared=NO;for(int j=0;j<i;j++)shared|=ms[i]==ms[j];
        if(shared)continue;
        int n=f*CHAIN+i;
        originals[n]=method_getImplementation(ms[i]);
        method_setImplementation(ms[i],chooses[n]);
    }
    installed[f]=YES;note("%s selector installed",family[f]);
    Class bar=objc_lookUpClass(f?"STUIStatusBar":"_UIStatusBar");
    Method sm=bar?class_getInstanceMethod(bar,sel_registerName("_effectiveStyleFromStyle:")):NULL;
    if(signature(sm,@encode(NSInteger),3,@encode(NSInteger))) {
        styleOriginal[f]=method_getImplementation(sm);
        method_setImplementation(sm,f?(IMP)style1:(IMP)style0);
        note("%s style observer installed",family[f]);
    }else badMethod(f,"optional _effectiveStyleFromStyle:",sm);
}
static dispatch_queue_t installQueue;
static atomic_bool installQueued;
static void installPending(void *unused) {
    atomic_store(&installQueued,false);
    installFamily(0);installFamily(1);installAppSafeArea();installHome();
    if(logFd>=0 && appSafeAreaScope)installAppTouchLog();
}
/* Probe on request: write a label to /private/var/tmp/sweetsb-probe. The foreground app and SpringBoard log their layout
   under that label. Only runs with the probe switch file. */
static char probeLast[64];
static void probeTick(void *unused) {
    static BOOL primed;   // a label written before this process started is not a request to it
    char label[48]="";
    int fd=open("/private/var/tmp/sweetsb-probe",O_RDONLY|O_CLOEXEC);
    ssize_t n=fd<0 ? 0 : read(fd,label,sizeof(label)-1);
    if(fd>=0)close(fd);
    label[n>0?n:0]=0;label[strcspn(label,"\r\n")]=0;
    BOOL fresh=primed && label[0] && strcmp(label,probeLast);
    primed=YES;
    snprintf(probeLast,sizeof(probeLast),"%s",label);
    if(!fresh)return;
    @autoreleasepool {
        if(!strcmp(logWho,"SpringBoard")) { probeHome(label); return; }
        id app=sweetSend((id)objc_getClass("UIApplication"),"sharedApplication");
        SEL st=sel_registerName("applicationState");
        if(!app || ![app respondsToSelector:st] || ((NSInteger(*)(id,SEL))objc_msgSend)(app,st)!=0)return;
        probeAppLayout(label);
        int n=0;
        for(id w in (NSArray *)sweetSend(app,"windows"))
            if(CGRectEqualToRect(((CGRect(*)(id,SEL))objc_msgSend)(w,sel_registerName("frame")),CGRectMake(0,0,420,930)))logTopButtons(w,0,&n,label);
    }
}
static void imageLoaded(const struct mach_header *header) {
    // Called under objc runtimeLock: never call runtime APIs or message objects here.
    if(!atomic_exchange(&installQueued,true))dispatch_async_f(installQueue,NULL,installPending);
}
static BOOL save(int starts) {
    return [[NSString stringWithFormat:@"%d %s\n",starts,boot]
        writeToFile:statePath atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}
#ifndef SWEETSB_HOST_TEST
__attribute__((constructor)) static void start(void) {
    unsetenv("DYLD_INSERT_LIBRARIES");
    openTee();
    @autoreleasepool { @try {
        NSString *exe=NSBundle.mainBundle.executablePath;
        if(!access("/private/var/tmp/sweetsb-probe.on",F_OK)) {
            snprintf(logWho,sizeof(logWho),"%s",exe?exe.lastPathComponent.UTF8String:"?");
            logFd=open("/private/var/tmp/sweetsb-real.log",O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC,0666);
        }
        BOOL spring=[exe isEqualToString:@"/System/Library/CoreServices/SpringBoard.app/SpringBoard"];
        NSString *home=NSHomeDirectory();
        note("loaded pid=%d exe=%s home=%s",getpid(),exe?exe.UTF8String:"nil",home?home.UTF8String:"nil");
        if (!spring && ![NSBundle.mainBundle.bundlePath hasSuffix:@".app"]) {note("scope rejected: not an app");return;}
        NSString *dir=spring ? @"/private/var/tmp" : [home stringByAppendingPathComponent:@"Library/Caches"];
        if (!spring && access(dir.fileSystemRepresentation,W_OK)) {   // a missing or foreign container home: SpringBoard proves /private/var/tmp
            note("guard state dir %s not writable (errno %d), using /private/var/tmp",dir.UTF8String,errno);
            dir=@"/private/var/tmp";
        }
        statePath=[(spring ? @"/private/var/tmp/sweetsb-real.state" :
            [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"sweetsb-real-%@.state",exe.lastPathComponent]]) copy];
        size_t len=sizeof(boot)-1;
        if (sysctlbyname("kern.bootsessionuuid",boot,&len,NULL,0) || !boot[0]) {note("guard failed: boot UUID unavailable");return;}
        int starts=0; char was[64]="";
        NSString *old=[NSString stringWithContentsOfFile:statePath encoding:NSUTF8StringEncoding error:NULL];
        BOOL damaged=old && sscanf(old.UTF8String,"%d %63s",&starts,was)!=2;
        starts=damaged ? SWEETSB_MAX_STARTS+1 : sweetsb_guard_next(starts,was,boot);   // a damaged record never reads as a fresh start
        if (!save(starts)) {note("guard failed: cannot write state %s",statePath.UTF8String);return;}
        if (!sweetsb_guard_allows(starts)) {note("guard blocked start=%d",starts);return;}
        appSafeAreaScope=sweetAppExecutable(exe.UTF8String);
        note("guard armed start=%d; registering ObjC image callback",starts);
        installQueue=dispatch_get_main_queue();
        objc_addLoadImageFunc(imageLoaded);
        note("image callback registered");
        // Install outside runtimeLock. In SpringBoard resolve a late STUI dependency
        // synchronously, before main can construct a bar and cache its provider.
        homeScope=spring;
        appSafeAreaSwitchOff=!access("/private/var/tmp/sweetsb-app.off",F_OK);
        if(!spring) {
            // A clean trip to the background is a healthy start too. Without it an app closed and killed within 3 s stays blocked.
            healthySeconds=3;
            // ast152: Maps lived about 15 s with a stuck main queue, never marked itself, and its next launch ran stock.
            healthyQueue=dispatch_get_global_queue(QOS_CLASS_UTILITY,0);
            [[NSNotificationCenter defaultCenter] addObserverForName:@"UIApplicationDidEnterBackgroundNotification" object:nil queue:nil
                usingBlock:^(NSNotification *n){ save(0); }];
            scheduleHealthy();
        }
        if(logFd>=0) {
            dispatch_source_t t=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
            dispatch_source_set_timer(t,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),NSEC_PER_SEC,100*NSEC_PER_MSEC);
            dispatch_source_set_event_handler_f(t,probeTick);
            dispatch_resume(t);
        }
        installFamily(0);installFamily(1);installAppSafeArea();installHome();
        if(logFd>=0 && appSafeAreaScope)installAppTouchLog();
        if(spring && !installed[1] && !rejected[1]) {
            void *h=dlopen("/System/Library/PrivateFrameworks/SystemStatusUI.framework/SystemStatusUI",RTLD_LAZY|RTLD_LOCAL);
            if(!h)note("STUI failed framework load: %s",dlerror());
            else {note("STUI framework loaded before main");installFamily(1);}
            // Intentionally retain the framework: installed IMPs refer to its classes.
        }
    } @catch(NSException *e) {note("failed initialization exception; guard remains armed");} }
}

#endif
