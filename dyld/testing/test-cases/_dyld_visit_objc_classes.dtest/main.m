
// BUILD:  $CC main.m -o $BUILD_DIR/_dyld_visit_objc_classes.exe -lobjc

// RUN:  ./_dyld_visit_objc_classes.exe

// If we have a shared cache, then check that we can find the objc classes in it

#include <mach-o/dyld_priv.h>

#import <Foundation/Foundation.h>

#include "test_support.h"

static bool haveDyldCache() {
    size_t unusedCacheLen;
    return (_dyld_get_shared_cache_range(&unusedCacheLen) != NULL);
}

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
  if ( !haveDyldCache() ) {
    PASS("no shared cache");
  }

  const void* objectClass = [NSObject class];
  __block uint32_t objectClassCount = 0;
  _dyld_visit_objc_classes(^(const void* classPtr) {
    if ( classPtr == objectClass )
      ++objectClassCount;
  });

  if ( objectClassCount == 0 ) {
    FAIL("Failed to find NSObject in the shared cache");
  }

  if ( objectClassCount > 1 ) {
    FAIL("Found too many NSObject's in the shared cache");
  }

  PASS("Success");
}
