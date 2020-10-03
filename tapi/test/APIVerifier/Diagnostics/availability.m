// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

#if __is_target_environment(macabi)
int maccatalyst_value;
#else
// CHECK: availability.m:[[@LINE+1]]:7: warning: 'maccatalyst_value' has incompatible definitions
float maccatalyst_value __attribute__((availability(macos, unavailable)));
#endif


// CHECK-NOT: warning: 'macos_value' has incompatible definitions
#if __is_target_environment(macabi)
int macos_value __attribute__((availability(maccatalyst, unavailable)));
#else
float macos_value;
#endif