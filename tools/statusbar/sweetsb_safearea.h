/* App-local, post-merge safe area. Never installed in SpringBoard. */
#include <math.h>
#ifndef SWEETSB_APP_SAFEAREA
#define SWEETSB_APP_SAFEAREA 0
#endif
// Set to 0 to disable. The ordinary build defaults off; build.sh safearea enables it.
static const double APP_TOP_SAFE_AREA_PT = SWEETSB_APP_SAFEAREA ? 48 + VERTICAL_SHIFT_PT : 0;
typedef struct UIEdgeInsets { double top,left,bottom,right; } SweetInsets;
static BOOL appSafeAreaScope, appSafeAreaInstalled, appSafeAreaRejected;
static IMP appSafeAreaOriginal, appOverlapOriginal;
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
   bar's top edge, the status bar manager's frame, the raw scene top, and the status bar overlap before and after the hook. */
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
        double overlapIn=NAN,overlapOut=NAN;
        if(appOverlapOriginal && view) {
            Class vc=objc_getClass("UIViewController");
            SEL o=sel_registerName("_statusBarOverlapAndMarginInfoForView:inWindow:");
            overlapIn=((SweetInsets(*)(id,SEL,id,id))appOverlapOriginal)(vc,o,view,w).top;
            overlapOut=((SweetInsets(*)(id,SEL,id,id))objc_msgSend)(vc,o,view,w).top;
        }
        note("app status %lums: manager frame y %g h %g; scene raw top %g; overlap %g -> %g",ms,
            CGRectIsNull(sb)?NAN:sb.origin.y,CGRectIsNull(sb)?NAN:sb.size.height,raw,overlapIn,overlapOut);
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
/* +[UIViewController _statusBarOverlapAndMarginInfoForView:inWindow:] (UIKitCore 0x1892ca648) returns the status bar overlap
   in top and the horizontal margins in left/right. It reads -[UIWindowScene _safeAreaInsetsForInterfaceOrientation:] or the status
   bar height, not the window, and __UIPresentationControllerBaseContentInsetsForControllersAndViewInWindow (0x1892cc07c) takes
   max(overlap, _viewSafeAreaInsetsFromScene) for _setContentOverlayInsets:. ast99: the second was 50, the root views still 68.
   Same window checks on the inWindow argument; only top changes, only downward.
   ponytail: a view below the window top gets the cap, not top minus the delta; fine while every root view starts at y 0. */
static SweetInsets appOverlapInsets(id cls,SEL cmd,id view,id window) {
    SweetInsets original=((SweetInsets(*)(id,SEL,id,id))appOverlapOriginal)(cls,cmd,view,window);
    if(!appSafeAreaScope || APP_TOP_SAFE_AREA_PT<=0)return original;
    if(!window) { if(APP_ONCE(24))note("app overlap skip: no window; top=%g",original.top); return original; }
    if(!appWindowFits(window,original,"app overlap",16))return original;
    if(!(isfinite(original.top) && original.top>APP_TOP_SAFE_AREA_PT && original.top<=80)) {
        if(APP_ONCE(22))note("app overlap unchanged: top %g left %g right %g",original.top,original.left,original.right);
        return original;
    }
    SweetInsets adjusted=original;adjusted.top=APP_TOP_SAFE_AREA_PT;
    if(APP_ONCE(23))note("app overlap top %g -> %g; view %s target=%gpt",original.top,adjusted.top,object_getClassName(view),APP_TOP_SAFE_AREA_PT);
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
    Method overlap=class_getClassMethod(objc_lookUpClass("UIViewController"),sel_registerName("_statusBarOverlapAndMarginInfoForView:inWindow:"));
    BOOL overlapOK=signature(overlap,@encode(SweetInsets),4,NULL);
    if(!overlapOK)note("app overlap failed ABI: %s",overlap?method_getTypeEncoding(overlap):"absent");
    appSafeAreaOriginal=method_getImplementation(hook);
    method_setImplementation(hook,(IMP)appSceneInsets);
    if(overlapOK) {
        appOverlapOriginal=method_getImplementation(overlap);
        method_setImplementation(overlap,(IMP)appOverlapInsets);
    }
    appSafeAreaInstalled=YES;
    note("app safe area installed; target=%gpt; portrait full-screen only; overlap route %s",APP_TOP_SAFE_AREA_PT,overlapOK?"on":"off");
}
