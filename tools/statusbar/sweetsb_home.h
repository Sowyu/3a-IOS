/* SpringBoard home screen top inset. SpringBoard only; apps never install it.
   The icon grid of a page is an SBIconListView. -[SBIconListView layoutInsetsForOrientation:] (SpringBoardHome 0x1c17f27e0) returns
   its layout's insets plus additionalLayoutInsets, and getLayoutMetricsParameters:orientation: (0x1c17f8e48) builds every icon
   position from the view bounds and that value. Lowering top and raising bottom by the same amount moves the grid up as a block:
   row spacing, the dock, the page dots and the search pill stay where they were.
   Off switches: HOME_TOP_SHIFT_PT 0 at build time, or /private/var/tmp/sweetsb-home.off before SpringBoard starts. */
#ifndef SWEETSB_HOME_SHIFT
#define SWEETSB_HOME_SHIFT 0
#endif
static const double HOME_TOP_SHIFT_PT = SWEETSB_HOME_SHIFT ? 18 : 0;   // the app scene went 68 -> 50
static BOOL homeScope, homeInstalled, homeRejected;
static IMP homeOriginal;
static atomic_uint homeReports;
#define HOME_ONCE(bit) (!(atomic_fetch_or(&homeReports,1u<<(bit))&(1u<<(bit))))
/* Pages of the home screen and the Today view. Dock, folders, App Library pods and widget sheets keep their insets. */
static int homeLocationIndex(id location) {
    static const char *const shifted[]={"SBIconLocationRoot","SBIconLocationRootWithWidgets","SBIconLocationTodayView"};
    if(!location || ![location isKindOfClass:[NSString class]])return -1;
    for(int i=0;i<3;i++)if(!strcmp([(NSString *)location UTF8String],shifted[i]))return i;
    return -1;
}
static SweetInsets homeShift(SweetInsets in,double shift) {
    // Only an ordinary portrait page inset that can give up the shift and stay non-negative.
    if(shift>0 && shift<=30 && isfinite(in.top) && isfinite(in.bottom) && in.top>=shift && in.top<=200) {
        in.top-=shift;
        in.bottom+=shift;
    }
    return in;
}
static int homeCanvas=-1;   // the screen never changes in SpringBoard's life
static BOOL homeOnCanvas(void) {
    if(homeCanvas<0)homeCanvas=canvas(1,sweetSend((id)objc_getClass("UIScreen"),"mainScreen"))?1:0;
    return homeCanvas==1;
}
static SweetInsets homeListInsets(id list,SEL cmd,NSInteger orientation) {
    SweetInsets original=((SweetInsets(*)(id,SEL,NSInteger))homeOriginal)(list,cmd,orientation);
    if(!homeScope || HOME_TOP_SHIFT_PT<=0 || orientation!=1)return original;
    id location=((id(*)(id,SEL))objc_msgSend)(list,sel_registerName("iconLocation"));
    int i=homeLocationIndex(location);
    if(i<0)return original;
    if(!homeOnCanvas())return original;
    SweetInsets adjusted=homeShift(original,HOME_TOP_SHIFT_PT);
    if(adjusted.top==original.top) {
        if(HOME_ONCE(i))note("home unchanged: %s insets %g,%g,%g,%g",[(NSString *)location UTF8String],original.top,original.left,original.bottom,original.right);
        return original;
    }
    if(HOME_ONCE(8+i)) {
        note("home top %g -> %g, bottom %g -> %g; %s",original.top,adjusted.top,original.bottom,adjusted.bottom,[(NSString *)location UTF8String]);
        scheduleHealthy();
    }
    return adjusted;
}
/* App Library: -[SBHLibrarySearchController _windowSafeAreaInsets] (0x1c1a691d0) is the window's safeAreaInsets, and
   _calculateSearchBarFrame:forExplicitVisualConfiguration: (0x1c1a6775c) puts the search bar 3 pt below its top. */
static IMP libraryOriginal;
static SweetInsets homeLibraryInsets(id controller,SEL cmd) {
    SweetInsets original=((SweetInsets(*)(id,SEL))libraryOriginal)(controller,cmd);
    // ast152: App Library laid out at SpringBoard start, before the grid hook had checked the screen, kept its 68 pt bar and
    // never laid out again. Each hook checks the screen itself.
    if(!homeScope || HOME_TOP_SHIFT_PT<=0 || !homeOnCanvas())return original;
    SweetInsets adjusted=original;
    if(isfinite(original.top) && original.top>=50+HOME_TOP_SHIFT_PT && original.top<=80 && original.left==0 && original.right==0)
        adjusted.top-=HOME_TOP_SHIFT_PT;   // a portrait notch top only
    if(HOME_ONCE(adjusted.top==original.top?4:12))note("home library search top %g -> %g",original.top,adjusted.top);
    return adjusted;
}
static void installHome(void) {
    if(homeInstalled || homeRejected || !homeScope)return;
    if(HOME_TOP_SHIFT_PT<=0) { homeRejected=YES; note("home shift off: build has shift 0"); return; }
    if(!access("/private/var/tmp/sweetsb-home.off",F_OK)) { homeRejected=YES; note("home shift off: /private/var/tmp/sweetsb-home.off"); return; }
    Class list=objc_lookUpClass("SBIconListView");
    if(!list) {
        if(HOME_ONCE(16))note("home waiting: SBIconListView absent");
        return;
    }
    Method m=class_getInstanceMethod(list,sel_registerName("layoutInsetsForOrientation:"));
    Method loc=class_getInstanceMethod(list,sel_registerName("iconLocation"));
    if(!signature(m,@encode(SweetInsets),3,@encode(NSInteger)) || !signature(loc,"@",2,NULL)) {
        note("home failed ABI: layoutInsetsForOrientation: %s; iconLocation %s",m?method_getTypeEncoding(m):"absent",loc?method_getTypeEncoding(loc):"absent");
        homeRejected=YES;return;
    }
    homeOriginal=method_getImplementation(m);
    method_setImplementation(m,(IMP)homeListInsets);
    homeInstalled=YES;
    // Optional: without it the pages still move and App Library keeps its stock top.
    Class search=objc_lookUpClass("SBHLibrarySearchController");
    Method lib=search?class_getInstanceMethod(search,sel_registerName("_windowSafeAreaInsets")):NULL;
    BOOL libOK=signature(lib,@encode(SweetInsets),2,NULL);
    if(libOK) {
        libraryOriginal=method_getImplementation(lib);
        method_setImplementation(lib,(IMP)homeLibraryInsets);
    }
    note("home shift installed; top -%gpt bottom +%gpt; portrait root and today pages; library %s",HOME_TOP_SHIFT_PT,HOME_TOP_SHIFT_PT,
        libOK?"on":lib?method_getTypeEncoding(lib):"absent");
}
/* Probe: visible icon lists, their insets before and after, the first icon's top, and search fields (App Library, Today). */
static CGRect inWindow(id v) {
    CGRect b=((CGRect(*)(id,SEL))objc_msgSend)(v,sel_registerName("bounds"));
    return ((CGRect(*)(id,SEL,CGRect,id))objc_msgSend)(v,sel_registerName("convertRect:toView:"),b,nil);
}
static BOOL shown(id v) {
    return !((BOOL(*)(id,SEL))objc_msgSend)(v,sel_registerName("isHidden")) &&
        ((double(*)(id,SEL))objc_msgSend)(v,sel_registerName("alpha"))>0.01;
}
static void homeWalk(id v,int depth,int *lines,const char *label) {
    if(!v || depth>40 || *lines>=40 || !shown(v))return;
    CGRect r=inWindow(v);
    if(!CGRectIntersectsRect(r,CGRectMake(0,0,420,930)))return;
    Class list=objc_getClass("SBIconListView");
    const char *name=object_getClassName(v);
    if(list && [v isKindOfClass:list]) {
        id location=sweetSend(v,"iconLocation");
        SEL s=sel_registerName("layoutInsetsForOrientation:");
        SweetInsets now=((SweetInsets(*)(id,SEL,NSInteger))objc_msgSend)(v,s,1);
        SweetInsets raw=homeOriginal?((SweetInsets(*)(id,SEL,NSInteger))homeOriginal)(v,s,1):now;
        double first=NAN;
        for(id sub in (NSArray *)sweetSend(v,"subviews")) {
            if(!shown(sub))continue;
            double y=inWindow(sub).origin.y;
            if(!(first<=y))first=y;
        }
        note("home %s: %s %s frame y %g h %g; insets %g,%g -> %g,%g (top,bottom); first item y %g",label,name,
            location?[[location description] UTF8String]:"nil",r.origin.y,r.size.height,raw.top,raw.bottom,now.top,now.bottom,first);
        (*lines)++;
        return;   // icons are leaves for this purpose
    }
    if(strstr(name,"SearchTextField") || strstr(name,"SearchBar") || strstr(name,"SearchField") || strstr(name,"SearchPill")) {
        note("home %s: %s frame y %g h %g",label,name,r.origin.y,r.size.height);
        (*lines)++;
        return;
    }
    for(id sub in (NSArray *)sweetSend(v,"subviews"))homeWalk(sub,depth+1,lines,label);
}
static void probeHome(const char *label) {
    id app=sweetSend((id)objc_getClass("UIApplication"),"sharedApplication");
    int lines=0,windows=0;
    for(id w in (NSArray *)sweetSend(app,"windows")) {
        CGRect f=((CGRect(*)(id,SEL))objc_msgSend)(w,sel_registerName("frame"));
        if(!CGRectEqualToRect(f,CGRectMake(0,0,420,930)) || !shown(w))continue;
        windows++;
        SEL sa=sel_registerName("safeAreaInsets");
        note("home %s: window %s safe top %g",label,object_getClassName(w),((SweetInsets(*)(id,SEL))objc_msgSend)(w,sa).top);
        homeWalk(w,0,&lines,label);
    }
    note("home %s: %d windows, %d lines",label,windows,lines);
}
