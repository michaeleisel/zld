#import <Foundation/Foundation.h>

// Test public global.
extern int publicGlobalVariable NS_AVAILABLE(NA, NA);

// Test public ObjC class
NS_CLASS_AVAILABLE(NA, NA)
@interface Foo : NSObject
@end

// Test unavailable attribute.
#ifdef __i386__
#define UNAVAILABLE_I386 __attribute__((unavailable))
#else
#define UNAVAILABLE_I386
#endif
extern int publicGlobalVariable2 UNAVAILABLE_I386;

extern int publicGlobalVariable3 __attribute__((unavailable))
__attribute__((availability(macosx, introduced = 10.9)));

// Test obsoleted with exported variable.
extern int publicGlobalVariable4 __attribute__((availability(
    macosx, introduced = 10.9, deprecated = 10.10, obsoleted = 10.11)));
// Test obsoleted with non-existent variable.
extern int publicGlobalVariable5 __attribute__((availability(
    macosx, introduced = 10.9, deprecated = 10.10, obsoleted = 10.11)));

#ifdef __i386__
#define OBSOLETE_I386 __attribute__((availability(macosx, obsoleted = 10.11)))
#else
#define OBSOLETE_I386
#endif
extern int publicGlobalVariable6 OBSOLETE_I386;
