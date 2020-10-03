// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

@protocol A1 @end
@protocol A2 @end

#if !__is_target_environment(macabi)
// CHECK: protocol.m:[[@LINE+2]]:11: warning: 'Test' has incompatible definitions
// CHECK: protocol.m:[[@LINE+1]]:17: note: 'Test' conforms to protocol 'A1'
@protocol Test <A1> @end
#else
// CHECK: protocol.m:[[@LINE+1]]:1: note: 'Test' has no corresponding protocol here
@protocol Test <A2> @end
#endif

#if !__is_target_environment(macabi)
// CHECK: protocol.m:[[@LINE+2]]:11: warning: 'Test1' has incompatible definitions
// CHECK: protocol.m:[[@LINE+1]]:1: note: 'Test1' has no corresponding protocol here
@protocol Test1 <A1> @end
#else
// CHECK: protocol.m:[[@LINE+1]]:18: note: 'Test1' conforms to protocol 'A2'
@protocol Test1 <A2, A1> @end
#endif

// CHECK-NOT: warning: 'Test2' has incompatible definitions
@protocol Test2
#if !__is_target_environment(macabi)
- (void)test __attribute__((availability(maccatalyst, unavailable)));
#endif
@end

// CHECK: protocol.m:[[@LINE+1]]:11: warning: 'Test3' has incompatible definitions
@protocol Test3
#if !__is_target_environment(macabi)
// CHECK: protocol.m:[[@LINE+1]]:4: note: return value has type 'void' here
- (void) test;
#else
// CHECK: protocol.m:[[@LINE+1]]:4: note: return value has type 'int' here
- (int) test;
#endif
@end

// CHECK: protocol.m:[[@LINE+1]]:11: warning: 'Test4' has incompatible definitions
@protocol Test4
#if !__is_target_environment(macabi)
// CHECK: protocol.m:[[@LINE+1]]:11: note: property 'test' has type 'int' here
@property int test;
#else
// CHECK: protocol.m:[[@LINE+1]]:11: note: property 'test' has type 'float' here
@property float test;
#endif
@end

@protocol Foo <Test> @end
