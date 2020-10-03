// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print -whitelist %S/Inputs/whitelist.yaml %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CHECK-WHITELIST --allow-empty

#if !__is_target_environment(macabi)
@interface Base1 @end;
#else
@interface Base2 @end;
#endif

// CHECK: whitelist.m:[[@LINE+2]]:12: warning: 'A' has incompatible definitions
#if !__is_target_environment(macabi)
@interface A : Base1 @end;
#else
@interface A : Base2 @end;
#endif

// CHECK: whitelist.m:[[@LINE+1]]:12: warning: 'B' has incompatible definitions
@interface B
#if !__is_target_environment(macabi)
- (void) b;
#else
- (id) b;
#endif
@end

// CHECK-WHITELIST-NOT: warning:
