/* App-local, post-merge safe area. Never installed in SpringBoard. */
#include <math.h>
#ifndef SWEETSB_APP_SAFEAREA
#define SWEETSB_APP_SAFEAREA 0
#endif
// Set to 0 to disable. The ordinary build defaults off; build.sh safearea enables it.
static const double APP_TOP_SAFE_AREA_PT = SWEETSB_APP_SAFEAREA ? 48 + VERTICAL_SHIFT_PT : 0;
typedef struct UIEdgeInsets { double top,left,bottom,right; } SweetInsets;
static BOOL appSafeAreaScope, appSafeAreaInstalled, appSafeAreaRejected;
static IMP appSafeAreaOriginal;
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
#define APP_SKIP(bit,...) do{ if(!(atomic_fetch_or(&appSafeAreaSkips,1u<<(bit))&(1u<<(bit))))note(__VA_ARGS__); return original; }while(0)
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
/* What layout actually used: window and root view safe area, and the first navigation bar's top edge. */
static void probeAppLayout(void *tag) {
    id app=sweetSend((id)objc_getClass("UIApplication"),"sharedApplication");
    int n=0;
    for(id w in (NSArray *)sweetSend(app,"windows")) {
        CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(w,sel_registerName("frame"));
        if(!CGRectEqualToRect(f,CGRectMake(0,0,420,930)) || n++>=3)continue;
        id root=sweetSend(w,"rootViewController"),view=sweetSend(root,"view"),bar=findNavigationBar(w,0);
        CGRect b=bar ? ((CGRect(*)(id,SEL,CGRect,id))objc_msgSend)(bar,sel_registerName("convertRect:toView:"),
            ((CGRect(*)(id,SEL))objc_msgSend)(bar,sel_registerName("bounds")),nil) : CGRectNull;
        note("app layout %lus: window %s safe top %g; root %s safe top %g; navbar y %g h %g",(unsigned long)(uintptr_t)tag,
            object_getClassName(w),safeTop(w),root?object_getClassName(root):"nil",safeTop(view),
            bar?b.origin.y:NAN,bar?b.size.height:NAN);
    }
    if(!n)note("app layout %lus: no full-screen window",(unsigned long)(uintptr_t)tag);
}
static SweetInsets appSceneInsets(id window,SEL cmd,BOOL includeStatusBar) {
    SweetInsets original=((SweetInsets(*)(id,SEL,BOOL))appSafeAreaOriginal)(window,cmd,includeStatusBar);
    if(!appSafeAreaScope || APP_TOP_SAFE_AREA_PT<=0)return original;
    if(((BOOL(*)(id,SEL))objc_msgSend)(window,sel_registerName("_isHostedInAnotherProcess")))
        APP_SKIP(0,"app safe area skip: hosted in another process; top=%g",original.top);
    double level=((double(*)(id,SEL))objc_msgSend)(window,sel_registerName("windowLevel"));
    if(level!=0)APP_SKIP(1,"app safe area skip: window level %g; top=%g",level,original.top);
    CGRect frame=((CGRect(*)(id,SEL))objc_msgSend)(window,sel_registerName("frame"));
    if(!CGRectEqualToRect(frame,CGRectMake(0,0,420,930)))
        APP_SKIP(2,"app safe area skip: frame %g,%g %gx%g; top=%g",frame.origin.x,frame.origin.y,frame.size.width,frame.size.height,original.top);
    id scene=((id(*)(id,SEL))objc_msgSend)(window,sel_registerName("windowScene"));
    if(!scene || ![scene isKindOfClass:objc_getClass("UIWindowScene")])
        APP_SKIP(3,"app safe area skip: scene %s; top=%g",scene?object_getClassName(scene):"nil",original.top);
    NSInteger orientation=((NSInteger(*)(id,SEL))objc_msgSend)(scene,sel_registerName("interfaceOrientation"));
    if(orientation!=1)APP_SKIP(4,"app safe area skip: orientation %ld; top=%g",(long)orientation,original.top);
    id screen=((id(*)(id,SEL))objc_msgSend)(window,sel_registerName("screen"));
    if(!canvas(0,screen))APP_SKIP(5,"app safe area skip: screen is not the 840x1860 canvas; top=%g",original.top);
    SweetInsets adjusted=compactAppInsets(original,APP_TOP_SAFE_AREA_PT);
    if(adjusted.top==original.top)
        APP_SKIP(includeStatusBar?6:7,"app safe area unchanged: statusbar=%d insets %g,%g,%g,%g",includeStatusBar,original.top,original.left,original.bottom,original.right);
    // once per route: iOS 26 apps on scene-setting safe areas pass NO here (_normalizedSafeAreaInsets)
    if(!(atomic_fetch_or(&appSafeAreaSkips,1u<<(8+!!includeStatusBar))&(1u<<(8+!!includeStatusBar)))) {
        note("app safe area top %g -> %g; statusbar=%d target=%gpt",original.top,adjusted.top,includeStatusBar,APP_TOP_SAFE_AREA_PT);
        if(!atomic_exchange(&appSafeAreaReported,true)) {
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),(void *)2,probeAppLayout);
            dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_main_queue(),(void *)10,probeAppLayout);
        }
    }
    scheduleHealthy();
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
    appSafeAreaOriginal=method_getImplementation(hook);
    method_setImplementation(hook,(IMP)appSceneInsets);
    appSafeAreaInstalled=YES;
    note("app safe area installed; target=%gpt; portrait full-screen only",APP_TOP_SAFE_AREA_PT);
}
