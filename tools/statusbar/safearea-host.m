#define SWEETSB_HOST_TEST 1
#define SWEETSB_APP_SAFEAREA 1
#define SWEETSB_HOME_SHIFT 1
#include "sweetsb.m"
#include <assert.h>
@interface UIScreen:NSObject
@property BOOL external;
@end
@implementation UIScreen
- (CGRect)nativeBounds{return CGRectMake(0,0,840,1860);}
- (double)scale{return 2;}
- (double)nativeScale{return 2;}
- (BOOL)_isEmbeddedScreen{return !_external;}
@end
@interface UIWindowScene:NSObject
@property NSInteger interfaceOrientation;
@property(retain) UIScreen *screen;
@property CGRect bounds;
@property double rawTop;
@end
@implementation UIWindowScene
- (CGRect)_referenceBounds{return _bounds;}
- (SweetInsets)_safeAreaInsetsForInterfaceOrientation:(NSInteger)o{return (SweetInsets){_rawTop,0,34,0};}
@end
@interface UIWindow:NSObject
@property(retain) UIWindowScene *windowScene;
@property(retain) UIScreen *screen;
@property CGRect frame;
@property double windowLevel;
@property BOOL hosted;
@property double top;
@end
@implementation UIWindow
- (BOOL)_isHostedInAnotherProcess{return _hosted;}
- (SweetInsets)_sceneSafeAreaInsetsIncludingStatusBar:(BOOL)yes{return (SweetInsets){_top+(yes?1:0),0,34,0};}
@end
@interface SBIconListView:NSObject
@property(copy) NSString *iconLocation;
@property double top;
@end
@implementation SBIconListView
- (SweetInsets)layoutInsetsForOrientation:(NSInteger)o{return (SweetInsets){_top,27,40,27};}
@end
@interface SBHLibrarySearchController:NSObject
@property double top;
@end
@implementation SBHLibrarySearchController
- (SweetInsets)_windowSafeAreaInsets{return (SweetInsets){_top,0,34,0};}
@end
static double libTop(SBHLibrarySearchController *c) {
    return ((SweetInsets(*)(id,SEL))objc_msgSend)(c,sel_registerName("_windowSafeAreaInsets")).top;
}
@implementation UIScreen (Main)
+ (id)mainScreen{static UIScreen *s;if(!s)s=[UIScreen new];return s;}
@end
static SweetInsets listInsets(SBIconListView *l,NSInteger o) {
    return ((SweetInsets(*)(id,SEL,NSInteger))objc_msgSend)(l,sel_registerName("layoutInsetsForOrientation:"),o);
}
static double rawTop(UIWindowScene *sc,NSInteger o) {
    return ((SweetInsets(*)(id,SEL,NSInteger))objc_msgSend)(sc,sel_registerName("_safeAreaInsetsForInterfaceOrientation:"),o).top;
}
static SweetInsets readInsets(UIWindow *w,BOOL yes) {
    return ((SweetInsets(*)(id,SEL,BOOL))objc_msgSend)(w,sel_registerName("_sceneSafeAreaInsetsIncludingStatusBar:"),yes);
}
int main(void){@autoreleasepool {
    assert(sweetAppExecutable("/Applications/Preferences.app/Preferences"));
    assert(sweetAppExecutable("/private/var/containers/Bundle/Application/UUID/Test.app/Test"));
    assert(!sweetAppExecutable("/System/Library/CoreServices/SpringBoard.app/SpringBoard"));
    assert(!sweetAppExecutable("/Applications/Test.app/PlugIns/Widget.appex/Widget"));
    assert(!sweetAppExecutable("/Applications/Test.app/Frameworks/WebKit.framework/WebContent"));
    assert(!sweetAppExecutable("/usr/libexec/backboardd"));
    assert(!sweetAppExecutable("/private/var/containers/Bundle/Application/../Test.app/Test"));
    installAppSafeArea();assert(!appSafeAreaInstalled); // SpringBoard/non-app scope
    atomic_store(&healthyScheduled,true); // no host guard writes/timers
    appSafeAreaScope=YES;installAppSafeArea();assert(appSafeAreaInstalled);
    IMP saved=appSafeAreaOriginal;installAppSafeArea();assert(saved==appSafeAreaOriginal);
    UIWindow *w=[UIWindow new];w.screen=[UIScreen new];w.windowScene=[UIWindowScene new];
    w.windowScene.interfaceOrientation=1;w.frame=CGRectMake(0,0,420,930);w.top=61;
    for(int n=0;n<2;n++) {
        SweetInsets in=readInsets(w,n);
        assert(in.top==APP_TOP_SAFE_AREA_PT && in.bottom==34 && in.left==0 && in.right==0);
    }
    w.windowScene.interfaceOrientation=3;assert(readInsets(w,YES).top==62);w.windowScene.interfaceOrientation=1;
    w.screen.external=YES;assert(readInsets(w,NO).top==61);w.screen.external=NO;
    w.hosted=YES;assert(readInsets(w,YES).top==62);w.hosted=NO;
    w.windowLevel=1;assert(readInsets(w,YES).top==62);w.windowLevel=0;
    w.frame=CGRectMake(0,0,420,500);assert(readInsets(w,YES).top==62);w.frame=CGRectMake(0,0,420,930);
    appSafeAreaScope=NO;assert(readInsets(w,YES).top==62);appSafeAreaScope=YES;
    w.top=90;assert(readInsets(w,YES).top==91);
    w.top=0;assert(readInsets(w,NO).top==0);
    w.top=61;
    UIWindowScene *sc=[UIWindowScene new];sc.interfaceOrientation=1;sc.screen=[UIScreen new];sc.bounds=CGRectMake(0,0,420,930);sc.rawTop=68;
    assert(appSceneOriginal && rawTop(sc,1)==APP_TOP_SAFE_AREA_PT);
    assert(rawTop(sc,3)==68);                                   // asked for landscape
    sc.interfaceOrientation=3;assert(rawTop(sc,1)==68);sc.interfaceOrientation=1;
    sc.bounds=CGRectMake(0,0,420,500);assert(rawTop(sc,1)==68);sc.bounds=CGRectMake(0,0,420,930);
    sc.screen.external=YES;assert(rawTop(sc,1)==68);sc.screen.external=NO;
    sc.rawTop=40;assert(rawTop(sc,1)==40);sc.rawTop=90;assert(rawTop(sc,1)==90);sc.rawTop=NAN;assert(isnan(rawTop(sc,1)));
    appSafeAreaScope=NO;sc.rawTop=68;assert(rawTop(sc,1)==68);appSafeAreaScope=YES;
    w.windowScene=nil;assert(readInsets(w,YES).top==62);
    SweetInsets in={62,0,34,0};assert(compactAppInsets(in,0).top==62);
    in.left=5;assert(compactAppInsets(in,54).top==62);
    in.left=0;in.top=NAN;assert(isnan(compactAppInsets(in,54).top));
    // Home: SpringBoard only, root and today pages, portrait, block shift
    SBIconListView *l=[SBIconListView new];l.iconLocation=@"SBIconLocationRoot";l.top=70;
    installHome();assert(!homeInstalled && listInsets(l,1).top==70);           // not SpringBoard
    homeScope=YES;installHome();assert(homeInstalled);
    IMP hs=homeOriginal;installHome();assert(hs==homeOriginal);
    SweetInsets h=listInsets(l,1);assert(h.top==52 && h.bottom==58 && h.left==27 && h.right==27);
    assert(listInsets(l,3).top==70);                                           // landscape
    l.iconLocation=@"SBIconLocationRootWithWidgets";assert(listInsets(l,1).top==52);
    l.iconLocation=@"SBIconLocationTodayView";assert(listInsets(l,1).top==52);
    l.iconLocation=@"SBIconLocationDock";assert(listInsets(l,1).top==70 && listInsets(l,1).bottom==40);
    l.iconLocation=@"SBIconLocationFolder";assert(listInsets(l,1).top==70);
    l.iconLocation=@"SBIconLocationAppLibraryCategoryPod";assert(listInsets(l,1).top==70);
    l.iconLocation=nil;assert(listInsets(l,1).top==70);
    l.iconLocation=@"SBIconLocationRoot";
    l.top=10;assert(listInsets(l,1).top==10);                                  // cannot give up 18 pt
    l.top=NAN;assert(isnan(listInsets(l,1).top));
    l.top=70;homeScope=NO;assert(listInsets(l,1).top==70);homeScope=YES;
    homeCanvas=0;assert(listInsets(l,1).top==70);homeCanvas=-1;
    ((UIScreen *)[UIScreen mainScreen]).external=YES;homeCanvas=-1;assert(listInsets(l,1).top==70);
    ((UIScreen *)[UIScreen mainScreen]).external=NO;homeCanvas=-1;assert(listInsets(l,1).top==52);
    SBHLibrarySearchController *lc=[SBHLibrarySearchController new];lc.top=68;
    homeCanvas=-1;assert(libraryOriginal && libTop(lc)==50 && homeCanvas==1);    // library first, before any grid call (ast152)
    lc.top=60;assert(libTop(lc)==60);lc.top=90;assert(libTop(lc)==90);lc.top=0;assert(libTop(lc)==0);lc.top=68;
    homeCanvas=0;assert(libTop(lc)==68);homeCanvas=1;
    homeScope=NO;assert(libTop(lc)==68);homeScope=YES;
    puts("PASS: app-only scope, real struct/BOOL hook ABI, top-only cap, off switch, idempotence, portrait/fullscreen/external/hosted/overlay guards, raw scene route, SpringBoard home block shift");
}}
