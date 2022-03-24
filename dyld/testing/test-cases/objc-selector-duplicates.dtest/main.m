
// BUILD:  $CC main.m -o $BUILD_DIR/objc-selector-duplicate.exe -lobjc -Wl,-no_objc_relative_method_lists -Wno-Wcast-of-sel-type

// RUN:  ./objc-selector-duplicate.exe

#include <mach-o/dyld_priv.h>
#include <objc/runtime.h>

#import <Foundation/NSObject.h>

#include "test_support.h"

// Selector strings might be duplicated in different cstring/meth_name sections.  Make sure we still
// unique them correctly.  This is one of the strings.  The other will come from the below class which puts the selector
// in __objc_methname
__attribute__((section(("__TEXT, __my_strings"))))
const char selString[] = "dyldClassFoo";

__attribute__((section(("__DATA, __objc_selrefs"))))
const char* mySelRef = &selString[0];

@interface DyldClass : NSObject
@end

@implementation DyldClass
-(void) dyldClassFoo {}
+(void) dyldClassFoo {}
@end

extern id objc_getClass(const char *name);

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    // dyldClassFoo
    const char* sel = _dyld_get_objc_selector("dyldClassFoo");
    if (sel) {
        if (sel != mySelRef) {
            FAIL("dyldClassFoo is wrong");
        }
    }

    unsigned int methodCount = 0;
    Method* methods = class_copyMethodList([DyldClass class], &methodCount);
    if ( methodCount != 1 )
        FAIL("dyldClassFoo method count is wrong.  Got %d", methodCount);

    const char* methodName = (const char*)method_getName(methods[0]);
    if (methodName != mySelRef) {
        FAIL("methodName is wrong pointer: %p(%s) vs %p(%s)", methodName, methodName, mySelRef, mySelRef);
    }


    PASS("objc-selector-duplicate");

    return 0;
}
