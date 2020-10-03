// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -diag-missing-api -no-print %s 2>&1 | FileCheck %s

#if __is_target_environment(macabi)
// CHECK: missing_api.m:[[@LINE+1]]:5: warning: 'foobar' is missing from target x86_64-apple-macos10.15 but required for target x86_64-apple-ios13.0-macabi
int foobar;
// CHECK: missing_api.m:[[@LINE+1]]:6: warning: 'foo' is missing from target x86_64-apple-macos10.15 but required for target x86_64-apple-ios13.0-macabi
void foo();
// CHECK: missing_api.m:[[@LINE+1]]:12: warning: 'Bar' is missing from target x86_64-apple-macos10.15 but required for target x86_64-apple-ios13.0-macabi
@interface Bar @end;
// CHECK: missing_api.m:[[@LINE+1]]:11: warning: 'Baz' is missing from target x86_64-apple-macos10.15 but required for target x86_64-apple-ios13.0-macabi
@protocol Baz @end
// CHECK: missing_api.m:[[@LINE+1]]:12: warning: 'Ext' is missing from target x86_64-apple-macos10.15 but required for target x86_64-apple-ios13.0-macabi
@interface Bar (Ext) @end

// CHECK-NOT: warning: 'unavailable' is missing from target
int unavailable __attribute__((availability(maccatalyst, unavailable)));
#endif
