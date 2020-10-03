#import <Foundation/Foundation.h>
#import "Basic.h"
#import "External.h"
#import "Simple.h"

// Public global variable
int publicGlobalVariable;

// Public weak global variable
int weakPublicGlobalVariable __attribute__((weak));

// Private global variable
int privateGlobalVariable;

// Private weak global variable
int weakPrivateGlobalVariable __attribute__((weak));

int extraGlobalAPI1, extraGlobalAPI2;

// ObjC Class
@implementation Simple
@end
@implementation Base
@end
@implementation SubClass
@end

__attribute__((objc_exception))
@interface SimpleInternalAPI : NSObject
@end
__attribute__((objc_exception))
@interface SimpleInternalSPI : NSObject
@end

@implementation SimpleInternalAPI
@end
@implementation SimpleInternalSPI
@end

@implementation Basic1
@end
@implementation Basic2
@end
@implementation Basic3
@synthesize property1;
@synthesize property2;
@synthesize property3;
@dynamic dynamicProp;
@end
@implementation Basic4
@end
@implementation Basic4_1
@end
@implementation Basic4_2
@end
@implementation Basic5
+ (void)aClassMethod {}
- (void)anInstanceMethod {}
@end
@implementation Basic6
@synthesize property1;
- (void)anInstanceMethodFromAnExtension {}
@end
@implementation Basic6 (Foo)
- (BOOL)property2 { return true; }
- (void)setProperty2:(BOOL) val {}
- (void)anInstanceMethodFromACategory {}
@end
@implementation Basic7
- (void)anInstanceMethodFromAnHiddenExtension {}
@end

@implementation ExternalManagedObject
- (void)foo {}
@end

@implementation NSManagedObject (Simple)
- (bool)supportsSimple { return true; }
@end

__attribute__((visibility("hidden")))
@interface HiddenClass : NSObject @end

@implementation HiddenClass @end

@implementation Basic8
+ (void)useSameName {}
- (void)useSameName {}
@end

@implementation A
- (void)aMethod {}
@end

@implementation Basic9
@end

@implementation Basic9 (deprecated)
@end

@protocol PrivateProtocol
- (void) privateProcotolMethod;
@end

@implementation FooClass
- (void) baseMethod {}
- (void) protocolMethod {}
- (void) barMethod{}
@end

@interface FooClass (Private) <PrivateProtocol>
@end

@implementation FooClass (Private)
- (void) privateProcotolMethod {}
@end
