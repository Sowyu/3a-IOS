/* sweetdhcp: bring a kernel Ethernet ifnet into iOS networking the way configd itself would, and check each layer an app uses.
 * Run as root over SSH. Steps run in order:
 *   dhcp IF          configd's IPConfiguration runs DHCP on IF (its ipconfig_set_service Mach call, as macOS "ipconfig setservice
 *                    IF DHCP"); the service it makes stays and IPMonitor ranks it like any other
 *   wait IF SECONDS  until IF has an IPv4 address
 *   state            configd's primary interface, router and DNS (State:/Network/Global/IPv4 and DNS)
 *   resolve NAME     getaddrinfo through mDNSResponder
 *   fetch URL        NSURLSession: status and byte count
 *   open URL         ask SpringBoard to open the URL (Safari for http/https)
 *   keys REGEX       dynamic-store keys matching REGEX, with values
 *   publish SERVICE IF ADDR MASK ROUTER DNS   write that service's IPv4 and DNS state, as sweetnetcfg does for 192.168.7.x
 * The IPConfiguration call layout is bootp's ipconfig.defs (subsystem 20000, routine 11), as HakiOS userland/src/net_test.m uses it. */
#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>

extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t, const char *, mach_port_t *);

#pragma pack(push, 4)
struct set_request {
	mach_msg_header_t head;
	mach_msg_body_t body;
	mach_msg_ool_descriptor_t method;
	NDR_record_t ndr;
	char name[16];
	mach_msg_type_number_t method_length;
};
struct set_reply {
	mach_msg_header_t head;
	NDR_record_t ndr;
	kern_return_t result;
	char service[128];
	int status;
	mach_msg_max_trailer_t trailer;
};
#pragma pack(pop)

static int dhcp(const char *ifname)
{
	NSData *method = [NSPropertyListSerialization dataWithPropertyList:@{@"ConfigMethod": @"DHCP"}
	    format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
	mach_port_t server = MACH_PORT_NULL, reply = mig_get_reply_port();
	union {struct set_request q; struct set_reply r;} m;
	kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.network.IPConfiguration", &server);
	if (kr != KERN_SUCCESS) {printf("dhcp: no IPConfiguration service: %#x\n", kr); return 1;}
	memset(&m, 0, sizeof m);
	m.q.head.msgh_bits = MACH_MSGH_BITS_COMPLEX | MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
	m.q.head.msgh_size = sizeof m.q;
	m.q.head.msgh_remote_port = server;
	m.q.head.msgh_local_port = reply;
	m.q.head.msgh_id = 20011;
	m.q.body.msgh_descriptor_count = 1;
	m.q.method.address = (void *)method.bytes;
	m.q.method.size = (mach_msg_size_t)method.length;
	m.q.method.copy = MACH_MSG_VIRTUAL_COPY;
	m.q.method.type = MACH_MSG_OOL_DESCRIPTOR;
	m.q.ndr = NDR_record;
	strlcpy(m.q.name, ifname, sizeof m.q.name);
	m.q.method_length = (mach_msg_type_number_t)method.length;
	kr = mach_msg(&m.q.head, MACH_SEND_MSG | MACH_RCV_MSG, sizeof m.q, sizeof m, reply, 10000, MACH_PORT_NULL);
	if (kr != KERN_SUCCESS || m.r.result != KERN_SUCCESS) {printf("dhcp: call failed: %#x %d\n", kr, m.r.result); return 1;}
	m.r.service[sizeof m.r.service - 1] = 0;
	printf("dhcp: setservice %s DHCP -> status %d (0 is done), service %s\n", ifname, m.r.status, m.r.service);
	return m.r.status != 0;
}

static int has_v4(const char *ifname, char *text, size_t size)
{
	struct ifaddrs *list, *a;
	int found = 0;
	if (getifaddrs(&list)) return 0;
	for (a = list; a; a = a->ifa_next)
		if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && !strcmp(a->ifa_name, ifname)) {
			inet_ntop(AF_INET, &((struct sockaddr_in *)a->ifa_addr)->sin_addr, text, (socklen_t)size);
			if (strncmp(text, "192.168.7.", 10)) {found = 1; break;}  /* the kernel's own static address does not count */
		}
	freeifaddrs(list);
	return found;
}

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
static void state(void)
{
	void *h = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
	SCDynamicStoreRef (*create)(CFAllocatorRef, CFStringRef, void *, void *) = dlsym(h, "SCDynamicStoreCreate");
	CFPropertyListRef (*copy)(SCDynamicStoreRef, CFStringRef) = dlsym(h, "SCDynamicStoreCopyValue");
	if (!create || !copy) {printf("state: no SCDynamicStore\n"); return;}
	SCDynamicStoreRef st = create(NULL, CFSTR("sweetdhcp"), NULL, NULL);
	for (NSString *k in @[@"State:/Network/Global/IPv4", @"State:/Network/Global/IPv6", @"State:/Network/Global/DNS"]) {
		CFPropertyListRef v = copy(st, (__bridge CFStringRef)k);
		printf("state: %s = %s\n", k.UTF8String, v ? [[(__bridge id)v description] UTF8String] : "(none)");
		if (v) CFRelease(v);
	}
}

static void *sc(const char *name)
{
	static void *h;
	if (!h) h = dlopen("/System/Library/Frameworks/SystemConfiguration.framework/SystemConfiguration", RTLD_NOW);
	return h ? dlsym(h, name) : NULL;
}

/* every dynamic-store key matching a regex, with its value */
static void keys(const char *pattern)
{
	SCDynamicStoreRef (*create)(CFAllocatorRef, CFStringRef, void *, void *) = sc("SCDynamicStoreCreate");
	CFArrayRef (*list)(SCDynamicStoreRef, CFStringRef) = sc("SCDynamicStoreCopyKeyList");
	CFPropertyListRef (*copy)(SCDynamicStoreRef, CFStringRef) = sc("SCDynamicStoreCopyValue");
	SCDynamicStoreRef st = create(NULL, CFSTR("sweetdhcp"), NULL, NULL);
	NSArray *k = CFBridgingRelease(list(st, (__bridge CFStringRef)@(pattern)));
	for (NSString *key in k) {
		CFPropertyListRef v = copy(st, (__bridge CFStringRef)key);
		printf("key %s = %s\n", key.UTF8String, v ? [[(__bridge id)v description] UTF8String] : "(none)");
		if (v) CFRelease(v);
	}
}

/* State:/Network/Service/SERVICE/{IPv4,DNS} and the service entry, as IPConfiguration would write them */
static int publish(const char *service, const char *ifname, const char *addr, const char *mask, const char *router, const char *dns)
{
	SCDynamicStoreRef (*create)(CFAllocatorRef, CFStringRef, void *, void *) = sc("SCDynamicStoreCreate");
	Boolean (*set)(SCDynamicStoreRef, CFStringRef, CFPropertyListRef) = sc("SCDynamicStoreSetValue");
	SCDynamicStoreRef st = create(NULL, CFSTR("sweetdhcp"), NULL, NULL);
	NSString *base = [NSString stringWithFormat:@"State:/Network/Service/%s", service];
	NSDictionary *v4 = @{@"Addresses": @[@(addr)], @"SubnetMasks": @[@(mask)], @"Router": @(router), @"InterfaceName": @(ifname)};
	BOOL ok = set(st, (__bridge CFStringRef)base, (__bridge CFDictionaryRef)@{@"InterfaceName": @(ifname)}) &&
	    set(st, (__bridge CFStringRef)[base stringByAppendingString:@"/IPv4"], (__bridge CFDictionaryRef)v4) &&
	    set(st, (__bridge CFStringRef)[base stringByAppendingString:@"/DNS"], (__bridge CFDictionaryRef)@{@"ServerAddresses": @[@(dns)]});
	printf("publish %s on %s: %s\n", service, ifname, ok ? "done" : "FAILED");
	return !ok;
}

static int resolve(const char *name)
{
	struct addrinfo hints = {.ai_socktype = SOCK_STREAM}, *list = NULL;
	int e = getaddrinfo(name, NULL, &hints, &list);
	if (e) {printf("resolve %s: %s\n", name, gai_strerror(e)); return 1;}
	char text[64];
	printf("resolve %s:", name);
	for (struct addrinfo *a = list; a; a = a->ai_next) {
		const void *p = a->ai_family == AF_INET ? (const void *)&((struct sockaddr_in *)a->ai_addr)->sin_addr :
		    (const void *)&((struct sockaddr_in6 *)a->ai_addr)->sin6_addr;
		printf(" %s", inet_ntop(a->ai_family, p, text, sizeof text));
	}
	printf("\n");
	freeaddrinfo(list);
	return 0;
}

static int fetch(const char *url)
{
	__block int rc = 1;
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	NSDate *start = [NSDate date];
	NSURLSessionConfiguration *c = [NSURLSessionConfiguration ephemeralSessionConfiguration];
	c.timeoutIntervalForRequest = 20;
	[[[NSURLSession sessionWithConfiguration:c] dataTaskWithURL:[NSURL URLWithString:@(url)]
	    completionHandler:^(NSData *data, NSURLResponse *r, NSError *err) {
		if (err) printf("fetch %s: %s\n", url, err.localizedDescription.UTF8String);
		else {
			printf("fetch %s: HTTP %ld, %lu bytes, %.2f s\n", url, (long)((NSHTTPURLResponse *)r).statusCode,
			    (unsigned long)data.length, -start.timeIntervalSinceNow);
			rc = 0;
		}
		dispatch_semaphore_signal(done);
	}] resume];
	dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
	return rc;
}

static int open_url(const char *url)
{
	dlopen("/System/Library/Frameworks/MobileCoreServices.framework/MobileCoreServices", RTLD_NOW);
	id ws = [NSClassFromString(@"LSApplicationWorkspace") performSelector:@selector(defaultWorkspace)];
	BOOL ok = [ws respondsToSelector:@selector(openURL:)] && ((BOOL (*)(id, SEL, NSURL *))objc_msgSend)(ws, @selector(openURL:),
	    [NSURL URLWithString:@(url)]);
	printf("open %s: %s\n", url, ok ? "asked" : "refused");
	return !ok;
}

int main(int argc, char **argv)
{
	@autoreleasepool {
		int rc = 0;
		setvbuf(stdout, NULL, _IOLBF, 0);
		for (int i = 1; i < argc; ++i) {
			const char *a = argv[i], *x = i + 1 < argc ? argv[i + 1] : NULL;
			if (!strcmp(a, "dhcp") && x) {rc |= dhcp(x); ++i;}
			else if (!strcmp(a, "wait") && x && i + 2 < argc) {
				char text[64] = "";
				int s = atoi(argv[i + 2]), ok = 0;
				for (int t = 0; t < s * 10 && !(ok = has_v4(x, text, sizeof text)); ++t) usleep(100000);
				printf("wait %s: %s\n", x, ok ? text : "no DHCP address");
				rc |= !ok;
				i += 2;
			} else if (!strcmp(a, "state")) state();
			else if (!strcmp(a, "resolve") && x) {rc |= resolve(x); ++i;}
			else if (!strcmp(a, "keys") && x) {keys(x); ++i;}
			else if (!strcmp(a, "publish") && i + 6 < argc) {
				rc |= publish(argv[i + 1], argv[i + 2], argv[i + 3], argv[i + 4], argv[i + 5], argv[i + 6]);
				i += 6;
			}
			else if (!strcmp(a, "fetch") && x) {rc |= fetch(x); ++i;}
			else if (!strcmp(a, "open") && x) {rc |= open_url(x); ++i;}
			else {fprintf(stderr, "usage: sweetdhcp [dhcp IF] [wait IF S] [state] [resolve NAME] [fetch URL] [open URL]\n"); return 2;}
		}
		return rc;
	}
}
