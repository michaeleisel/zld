// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -diag-missing-api -no-print %s 2>&1 | FileCheck %s

// CHECK-NOT: warning: 'Test' has incompatible definitions
@interface Test
#if !__is_target_environment(macabi)
- (void) method; // missing from variant target, OK.
#endif
@end

// CHECK: missing.m:[[@LINE+1]]:12: warning: 'Test1' has incompatible definitions
@interface Test1
// CHECK: missing.m:[[@LINE-1]]:1: note: 'Test1' has no corresponding method here
#if __is_target_environment(macabi)
// CHECK: missing.m:[[@LINE+1]]:1: note: 'Test1' has method 'miss' here
- (void) miss; // missing from base target, WRONG.
#endif
@end

// CHECK-NOT: warning: 'Test2' has incompatible definitions
@interface Test2
#if !__is_target_environment(macabi)
- (void) method; // incompatible but unavailable from variant, OK
#else
- (int)method __attribute__((availability(maccatalyst, unavailable)));
#endif
@end

// CHECK: missing.m:[[@LINE+1]]:12: warning: 'Test3' has incompatible definitions
@interface Test3
#if !__is_target_environment(macabi)
// CHECK: missing.m:[[@LINE+1]]:4: note: return value has type 'int' here
- (int) method __attribute__((availability(macos, unavailable)));
#else
// CHECK: missing.m:[[@LINE+1]]:4: note: return value has type 'void' here
- (void) method; // unavailable from base but available from variant, check ABI.
#endif
@end
