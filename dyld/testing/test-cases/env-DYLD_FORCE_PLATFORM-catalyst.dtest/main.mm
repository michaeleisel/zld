// BUILD(macos):  $CXX main.mm  -o $BUILD_DIR/env-DYLD_FORCE_PLATFORM-catalyst.exe -std=c++14 -framework Foundation
// BUILD(macos):  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/env-DYLD_FORCE_PLATFORM-catalyst.exe
// BUILD(ios,tvos,watchos,bridgeos):

// RUN:  DYLD_FORCE_PLATFORM=6 ./env-DYLD_FORCE_PLATFORM-catalyst.exe

#import <Foundation/Foundation.h>
#import <CoreFoundation/CFPriv.h>
#import <mach-o/dyld_priv.h>

#include "test_support.h"

__attribute__((section("__DATA,__allow_alt_plat"))) char dummy;

int main(int argc, const char * argv[]) {
    NSLog(@"Linked on or after Lion: %d", _CFExecutableLinkedOnOrAfter(CFSystemVersionLion));
    [[NSMutableArray new] removeLastObject];
    PASS("Success");
    return 0;
}
