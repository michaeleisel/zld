// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

@interface Foo @end

@protocol P1 @end
@protocol P2 @end

#if !__is_target_environment(macabi)
// CHECK: category.m:[[@LINE+2]]:12: warning: 'Test' has incompatible definitions
// CHECK: category.m:[[@LINE+1]]:24: note: 'Test' conforms to protocol 'P1'
@interface Foo (Test) <P1> @end
#else
// CHECK: category.m:[[@LINE+1]]:1: note: 'Test' has no corresponding protocol here
@interface Foo (Test) <P2> @end
#endif

#if !__is_target_environment(macabi)
// CHECK: category.m:[[@LINE+2]]:12: warning: 'Test1' has incompatible definitions
// CHECK: category.m:[[@LINE+1]]:29: note: 'Test1' conforms to protocol 'P2'
@interface Foo (Test1) <P1, P2> @end
#else
// CHECK: category.m:[[@LINE+1]]:1: note: 'Test1' has no corresponding protocol here
@interface Foo (Test1) <P1> @end
#endif

// CHECK-NOT: warning: 'Test2' has incompatible definitions
@interface Foo (Test2)
#if !__is_target_environment(macabi)
- (void)test __attribute__((availability(maccatalyst, unavailable)));
#endif
@end

// CHECK: category.m:[[@LINE+1]]:12: warning: 'Test3' has incompatible definitions
@interface Foo (Test3)
#if !__is_target_environment(macabi)
// CHECK: category.m:[[@LINE+1]]:4: note: return value has type 'void' here
- (void) test;
#else
// CHECK: category.m:[[@LINE+1]]:4: note: return value has type 'int' here
- (int) test;
#endif
@end

// CHECK: category.m:[[@LINE+1]]:12: warning: 'Foo' has incompatible definitions
@interface Foo ()
#if !__is_target_environment(macabi)
// CHECK: category.m:[[@LINE+1]]:4: note: return value has type 'void' here
- (void) test;
#else
// CHECK: category.m:[[@LINE+1]]:4: note: return value has type 'int' here
- (int) test;
#endif
@end
