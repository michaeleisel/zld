// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck --allow-empty %s

// CHECK-NOT: error:
// CHECK-NOT: warning:
@class Foo;

#if !__is_target_environment(macabi)
extern void foo(Foo *);
#else
extern void foo(Foo *);
#endif

@interface Test1
- (int)conformsToProtocol:(Protocol *)aProtocol;
@end

#if !__is_target_environment(macabi)
extern Test1 *a;
#else
extern Test1 *a;
#endif

@protocol Bar;
#if !__is_target_environment(macabi)
@interface Test2
- (id<Bar>)_bar;
@end
#else
@interface Test2
- (id<Bar>)_bar;
@end
#endif
