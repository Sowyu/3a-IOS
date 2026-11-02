/* App-local, post-merge safe area. Never installed in SpringBoard. */
#include <math.h>
#ifndef SWEETSB_APP_SAFEAREA
#define SWEETSB_APP_SAFEAREA 0
#endif
// Set to 0 to disable. The ordinary build defaults off; build.sh safearea enables it.
static const double APP_TOP_SAFE_AREA_PT = SWEETSB_APP_SAFEAREA ? 48 + VERTICAL_SHIFT_PT : 0;
typedef struct UIEdgeInsets { double top,left,bottom,right; } SweetInsets;
static BOOL appSafeAreaScope, appSafeAreaInstalled, appSafeAreaRejected;
static IMP appSafeAreaOriginal, appSceneOriginal;
static atomic_bool appSafeAreaReported;

static BOOL sweetAppExecutable(const char *p) {
    if(!p)return NO;
    if(!strncmp(p,"/Applications/",14))p+=14;
    else {
        const char *root="/private/var/containers/Bundle/Application/";
        if(strncmp(p,root,strlen(root)))return NO;
        p+=strlen(root);
        const char *slash=strchr(p,'/');
        if(!slash || slash==p || (slash-p==1 && *p=='.') || (slash-p==2 && !strncmp(p,"..",2)))return NO;
        p=slash+1;
    }
    const char *slash=strchr(p,'/');
    if(!slash || slash-p<5 || strncmp(slash-4,".app",4))return NO;
    p=slash+1;
    return *p && !strchr(p,'/') && strcmp(p,".") && strcmp(p,"..");
}
static SweetInsets compactAppInsets(SweetInsets in,double target) {
    // Only reduce ordinary portrait insets. Large overlays and zero/fullscreen
    // insets pass through. Revisit the 80 pt ceiling if another screen is supported.
    if(target>=48 && target<=80 && isfinite(in.top) && in.top>target && in.top<=80 &&
       in.left==0 && in.right==0)in.top=target;
    return in;
}
/* Each early return reports once per process, so a run shows which check kept the stock top. */
static atomic_uint appSafeAreaSkips;
#define APP_ONCE(bit) (!(atomic_fetch_or(&appSafeAreaSkips,1u<<(bit))&(1u<<(bit))))
#define APP_SKIP(bit,...) do{ if(APP_ONCE(bit))note(__VA_ARGS__); return NO; }while(0)
#define APP_RAW_SKIP(bit,...) do{ if(APP_ONCE(bit))note(__VA_ARGS__); return original; }while(0)
static id sweetSend(id o,const char *sel) {
    SEL s=sel_registerName(sel);
    return o && [o respondsToSelector:s] ? ((id(*)(id,SEL))objc_msgSend)(o,s) : nil;
}
static double safeTop(id view) {
    SEL s=sel_registerName("safeAreaInsets");
    return view && [view respondsToSelector:s] ? ((SweetInsets(*)(id,SEL))objc_msgSend)(view,s).top : NAN;
}
static id findNavigationBar(id view,int depth) {
    Class bar=objc_getClass("UINavigationBar");
    if(!view || !bar || depth>14)return nil;
    if([view isKindOfClass:bar])return view;
    for(id sub in (NSArray *)sweetSend(view,"subviews")) { id f=findNavigationBar(sub,depth+1); if(f)return f; }
    return nil;
}
/* What layout actually used, as a timeline from the first change: window and root view safe area, the first navigation
   bar's top edge, the status bar manager's frame, the scene top before and after the hook, and the presentation overlap. */
static void probeAppLayout(void *tag) {
    unsigned long ms=(unsigned long)(uintptr_t)tag;
    id app=sweetSend((id)objc_getClass("UIApplication"),"sharedApplication");
    int n=0;
    for(id w in (NSArray *)sweetSend(app,"windows")) {
        CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(w,sel_registerName("frame"));
        if(!CGRectEqualToRect(f,CGRectMake(0,0,420,930)) || n++>=3)continue;
        id root=sweetSend(w,"rootViewController"),view=sweetSend(root,"view"),bar=findNavigationBar(w,0);
        CGRect b=bar ? ((CGRect(*)(id,SEL,CGRect,id))objc_msgSend)(bar,sel_registerName("convertRect:toView:"),
            ((CGRect(*)(id,SEL))objc_msgSend)(bar,sel_registerName("bounds")),nil) : CGRectNull;
        note("app layout %lums: window %s safe top %g; root %s safe top %g; navbar y %g h %g",ms,
            object_getClassName(w),safeTop(w),root?object_getClassName(root):"nil",safeTop(view),
            bar?b.origin.y:NAN,bar?b.size.height:NAN);
        id scene=sweetSend(w,"windowScene"),manager=sweetSend(scene,"statusBarManager");
        SEL frameSel=sel_registerName("statusBarFrame"),rawSel=sel_registerName("_safeAreaInsetsForInterfaceOrientation:");
        CGRect sb=manager && [manager respondsToSelector:frameSel] ? ((CGRect(*)(id,SEL))objc_msgSend)(manager,frameSel) : CGRectNull;
        double raw=scene && [scene respondsToSelector:rawSel] ? ((SweetInsets(*)(id,SEL,NSInteger))objc_msgSend)(scene,rawSel,1).top : NAN;
        double raw0=NAN,overlap=NAN;
        if(appSceneOriginal && scene)raw0=((SweetInsets(*)(id,SEL,NSInteger))appSceneOriginal)(scene,rawSel,1).top;
        Class pc=objc_getClass("UIPresentationController");
        SEL o=sel_registerName("_statusBarOverlapAndMarginInfoForView:inWindow:");
        if(view && pc && [pc respondsToSelector:o])overlap=((SweetInsets(*)(id,SEL,id,id))objc_msgSend)(pc,o,view,w).top;
        note("app status %lums: manager frame y %g h %g; scene top %g -> %g; presentation overlap %g",ms,
            CGRectIsNull(sb)?NAN:sb.origin.y,CGRectIsNull(sb)?NAN:sb.size.height,raw0,raw,overlap);
    }
    if(!n)note("app layout %lums: no full-screen window",ms);
}
/* Portrait full-screen app window on the canvas. who/base: one report per check per caller (window bits 0-5, VC 16-21). */
static BOOL appWindowFits(id window,SweetInsets original,const char *who,unsigned base) {
    if(((BOOL(*)(id,SEL))objc_msgSend)(window,sel_registerName("_isHostedInAnotherProcess")))
        APP_SKIP(base,"%s skip: hosted in another process; top=%g",who,original.top);
    double level=((double(*)(id,SEL))objc_msgSend)(window,sel_registerName("windowLevel"));
    if(level!=0)APP_SKIP(base+1,"%s skip: window level %g; top=%g",who,level,original.top);
    CGRect frame=((CGRect(*)(id,SEL))objc_msgSend)(window,sel_registerName("frame"));
    if(!CGRectEqualToRect(frame,CGRectMake(0,0,420,930)))
        APP_SKIP(base+2,"%s skip: frame %g,%g %gx%g; top=%g",who,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height,original.top);
    id scene=((id(*)(id,SEL))objc_msgSend)(window,sel_registerName("windowScene"));
    if(!scene || ![scene isKindOfClass:objc_getClass("UIWindowScene")])
        APP_SKIP(base+3,"%s skip: scene %s; top=%g",who,scene?object_getClassName(scene):"nil",original.top);
    NSInteger orientation=((NSInteger(*)(id,SEL))objc_msgSend)(scene,sel_registerName("interfaceOrientation"));
    if(orientation!=1)APP_SKIP(base+4,"%s skip: orientation %ld; top=%g",who,(long)orientation,original.top);
    id screen=((id(*)(id,SEL))objc_msgSend)(window,sel_registerName("screen"));
    if(!canvas(0,screen))APP_SKIP(base+5,"%s skip: screen is not the 840x1860 canvas; top=%g",who,original.top);
    return YES;
}
static void appSafeAreaChanged(void) {
    if(!atomic_exchange(&appSafeAreaReported,true)) {
        static const unsigned ms[]={0,250,500,1000,2000,4000,10000};   // 0: the next main-queue turn, before the first frame
        for(unsigned i=0;i<sizeof(ms)/sizeof(*ms);i++)
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,(int64_t)ms[i]*NSEC_PER_MSEC),dispatch_get_main_queue(),(void *)(uintptr_t)ms[i],probeAppLayout);
    }
    scheduleHealthy();
}
static SweetInsets appSceneInsets(id window,SEL cmd,BOOL includeStatusBar) {
    SweetInsets original=((SweetInsets(*)(id,SEL,BOOL))appSafeAreaOriginal)(window,cmd,includeStatusBar);
    if(!appSafeAreaScope || APP_TOP_SAFE_AREA_PT<=0 || !appWindowFits(window,original,"app safe area",0))return original;
    SweetInsets adjusted=compactAppInsets(original,APP_TOP_SAFE_AREA_PT);
    if(adjusted.top==original.top) {
        if(APP_ONCE(includeStatusBar?6:7))note("app safe area unchanged: statusbar=%d insets %g,%g,%g,%g",includeStatusBar,original.top,original.left,original.bottom,original.right);
        return original;
    }
    // once per route: iOS 26 apps on scene-setting safe areas pass NO here (_normalizedSafeAreaInsets)
    if(APP_ONCE(8+!!includeStatusBar))
        note("app safe area top %g -> %g; statusbar=%d target=%gpt",original.top,adjusted.top,includeStatusBar,APP_TOP_SAFE_AREA_PT);
    appSafeAreaChanged();
    return adjusted;
}
/* -[UIWindowScene _safeAreaInsetsForInterfaceOrientation:] (UIKitCore 0x18918c360, only UIWindowScene implements it) is the raw
   scene top, 68 on this canvas. Three readers skip the window: _sceneSafeAreaInsetsIncludingStatusBar: (0x18918473c, hooked above),
   -[UIViewController _viewSafeAreaInsetsFromScene] for root controllers (0x1892cbe9c), and
   +[UIPresentationController _statusBarOverlapAndMarginInfoForView:inWindow:] (0x1892ca798), whose max with the window value made
   the root views 68 in ast97 to ast106. The status bar manager already reports 48 (ast106), so only this number is tall.
   Scene-wide: every window of a portrait full-screen app scene on the canvas, overlays included, sees the lower top. */
static SweetInsets appSceneRawInsets(id scene,SEL cmd,NSInteger orientation) {
    SweetInsets original=((SweetInsets(*)(id,SEL,NSInteger))appSceneOriginal)(scene,cmd,orientation);
    if(!appSafeAreaScope || APP_TOP_SAFE_AREA_PT<=0)return original;
    if(orientation!=1)APP_RAW_SKIP(16,"app scene skip: asked orientation %ld; top=%g",(long)orientation,original.top);
    NSInteger current=((NSInteger(*)(id,SEL))objc_msgSend)(scene,sel_registerName("interfaceOrientation"));
    if(current!=1)APP_RAW_SKIP(17,"app scene skip: orientation %ld; top=%g",(long)current,original.top);
    CGRect bounds=((CGRect(*)(id,SEL))objc_msgSend)(scene,sel_registerName("_referenceBounds"));
    if(!CGRectEqualToRect(bounds,CGRectMake(0,0,420,930)))
        APP_RAW_SKIP(18,"app scene skip: bounds %g,%g %gx%g; top=%g",bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height,original.top);
    id screen=((id(*)(id,SEL))objc_msgSend)(scene,sel_registerName("screen"));
    if(!canvas(0,screen))APP_RAW_SKIP(19,"app scene skip: screen is not the 840x1860 canvas; top=%g",original.top);
    SweetInsets adjusted=compactAppInsets(original,APP_TOP_SAFE_AREA_PT);
    if(adjusted.top==original.top) {
        if(APP_ONCE(22))note("app scene unchanged: insets %g,%g,%g,%g",original.top,original.left,original.bottom,original.right);
        return original;
    }
    if(APP_ONCE(23))note("app scene top %g -> %g; target=%gpt",original.top,adjusted.top,APP_TOP_SAFE_AREA_PT);
    appSafeAreaChanged();
    return adjusted;
}
static void installAppSafeArea(void) {
    if(appSafeAreaInstalled || appSafeAreaRejected)return;
    if(!appSafeAreaScope || APP_TOP_SAFE_AREA_PT<=0) {
        if(!atomic_exchange(&appSafeAreaReported,true))note("app safe area off: %s",appSafeAreaScope?"build has target 0":"not a top-level app executable");
        return;
    }
    Class window=objc_lookUpClass("UIWindow"),scene=objc_lookUpClass("UIWindowScene");
    if(!window||!scene) {  // Existing image-load callback retries outside runtimeLock.
        if(!(atomic_fetch_or(&appSafeAreaSkips,1u<<10)&(1u<<10)))note("app safe area waiting: UIKit classes absent");
        return;
    }
    const char *names[]={"_sceneSafeAreaInsetsIncludingStatusBar:","windowScene","screen","frame","windowLevel","_isHostedInAnotherProcess","interfaceOrientation"};
    const char *types[]={@encode(SweetInsets),"@","@",@encode(CGRect),"d",@encode(BOOL),@encode(NSInteger)};
    Method hook=NULL;
    for(int i=0;i<7;i++) {
        Method m=class_getInstanceMethod(i==6?scene:window,sel_registerName(names[i]));
        if(!signature(m,types[i],i==0?3:2,i==0?@encode(BOOL):NULL)) {
            note("app safe area failed ABI %s: %s",names[i],m?method_getTypeEncoding(m):"absent");
            appSafeAreaRejected=YES;return;
        }
        if(i==0)hook=m;
    }
    // Optional second route: without it the window hook still installs.
    Method raw=class_getInstanceMethod(scene,sel_registerName("_safeAreaInsetsForInterfaceOrientation:"));
    Method ref=class_getInstanceMethod(scene,sel_registerName("_referenceBounds"));
    Method scr=class_getInstanceMethod(scene,sel_registerName("screen"));
    BOOL rawOK=signature(raw,@encode(SweetInsets),3,@encode(NSInteger)) && signature(ref,@encode(CGRect),2,NULL) && signature(scr,"@",2,NULL);
    if(!rawOK)note("app scene failed ABI: %s",raw?method_getTypeEncoding(raw):"absent");
    appSafeAreaOriginal=method_getImplementation(hook);
    method_setImplementation(hook,(IMP)appSceneInsets);
    if(rawOK) {
        appSceneOriginal=method_getImplementation(raw);
        method_setImplementation(raw,(IMP)appSceneRawInsets);
    }
    appSafeAreaInstalled=YES;
    note("app safe area installed; target=%gpt; portrait full-screen only; scene route %s",APP_TOP_SAFE_AREA_PT,rawOK?"on":"off");
}
