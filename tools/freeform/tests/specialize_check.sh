#!/bin/bash
# Host test of bgmtl.m's "BEGIN specialize" block, cut out of next-ff's source and run against this Mac's Metal.framework:
# the constant setters are hooked on the real class cluster, values recorded per object, and the library hook hands back the
# plain function carrying them as air_constant_value words. Off without BGMTL_SPECIALIZE=1.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); src="$here/../next-ff/Chlorophyll/src/bgmtl.m"
t=$(mktemp -d); trap '/usr/bin/trash "$t"' EXIT
{
	cat <<'H'
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <os/lock.h>
#include <assert.h>
#define BG_LOG(...) fprintf(stderr, __VA_ARGS__)
H
	sed -n '/^\/\/ BEGIN specialize/,/^\/\/ END specialize/p' "$src"
	cat <<'T'
@interface FakeLib : NSObject
- (id)newFunctionWithName:(NSString *)n;
- (id)newFunctionWithName:(NSString *)n constantValues:(id)v error:(NSError **)e;
@end
@implementation FakeLib
- (id)newFunctionWithName:(NSString *)n { return [n isEqualToString:@"missing"] ? nil : [[NSObject alloc] init]; }
- (id)newFunctionWithName:(NSString *)n constantValues:(id)v error:(NSError **)e { (void)n; (void)v; (void)e; return nil; }   // Apple's: nil without a compiler
@end
int main(int argc, char **argv)
{
	@autoreleasepool {
		FakeLib *lib = [[FakeLib alloc] init];
		MTLFunctionConstantValues *v = [[MTLFunctionConstantValues alloc] init];
		bool t = true; uint32_t idx = 4; uint32_t r[2] = { 7, 9 };
		if (argc > 1) {   // off: no hook, Apple's path untouched
			bg_specialize_install([lib class]);
			assert([lib newFunctionWithName:@"f" constantValues:v error:NULL] == nil);
			puts("off: library and setters untouched");
			return 0;
		}
		bg_specialize_install([lib class]);
		bg_specialize_install([lib class]);   // once only
		[v setConstantValue:&t type:MTLDataTypeBool atIndex:2];
		[v setConstantValue:&idx type:MTLDataTypeUInt atIndex:13];
		[v setConstantValues:r type:MTLDataTypeUInt withRange:NSMakeRange(20, 2)];
		NSError *err = nil;
		id fn = [lib newFunctionWithName:@"uber_fragment" constantValues:v error:&err];
		assert(fn && !err);
		NSData *k = bg_function_constants(fn);
		const struct bg_constant *c = k.bytes;
		assert(k.length == 4 * sizeof(*c));
		assert(c[0].index == 2 && c[0].bits == 1 && c[1].index == 13 && c[1].bits == 4);
		assert(c[2].index == 20 && c[2].bits == 7 && c[3].index == 21 && c[3].bits == 9);
		assert(bg_function_constants([lib newFunctionWithName:@"plain"]) == nil);
		assert([lib newFunctionWithName:@"missing" constantValues:v error:&err] == nil && err);
		// Apple's own MTLFunctionConstantValues still works after the hook
		MTLFunctionConstantValues *copy = [v copy];
		assert(copy != nil);
		puts("on: setters recorded, function carries 4 constants sorted by index, unspecialized functions carry none");
	}
	return 0;
}
T
} > "$t/s.m"
clang -fno-objc-arc -Wall -Wno-unused-function -framework Foundation -framework Metal "$t/s.m" -o "$t/s"
BGMTL_SPECIALIZE=1 "$t/s"
"$t/s" off
