// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

@interface Foo
// CHECK-NOT: sameClassMethod
// CHECK-NOT: sameInstanceMethod
+ (void)sameClassMethod;
- (void)sameInstanceMethod;
@end

// CHECK: methods.m:[[@LINE+1]]:12: warning: 'Test' has incompatible definitions
@interface Test
#if !__is_target_environment(macabi)
// CHECK: methods.m:[[@LINE+1]]:4: note: return value has type 'int' here
- (int)mismatchRet;
#else
// CHECK: methods.m:[[@LINE+1]]:4: note: return value has type 'void' here
- (void)mismatchRet;
#endif
@end

// CHECK: methods.m:[[@LINE+1]]:12: warning: 'Test1' has incompatible definitions
@interface Test1
#if !__is_target_environment(macabi)
// CHECK: methods.m:[[@LINE+1]]:24: note: parameter has type 'int' here
- (void)mismatchParam:(int)arg;
#else
// CHECK: methods.m:[[@LINE+1]]:24: note: parameter has type 'float' here
- (void)mismatchParam:(float)arg;
#endif
@end

// CHECK-NOT: warning: 'Test2' has incompatible definitions
@interface Test2
#if !__is_target_environment(macabi)
- (void)test __attribute__((availability(maccatalyst, unavailable)));
#else
- (void)test1 __attribute__((availability(maccatalyst, introduced = 1.0)));
#endif
@end

