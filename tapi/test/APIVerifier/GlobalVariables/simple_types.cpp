// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -std=c++11 -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
// CHECK: simple_types.cpp:[[@LINE+2]]:12: warning: 'a' has incompatible definitions
// CHECK: simple_types.cpp:[[@LINE+1]]:8: note: variable 'a' has type 'int' here
extern int a;
#else
// CHECK: simple_types.cpp:[[@LINE+1]]:8: note: variable 'a' has type 'short' here
extern short a;
#endif

// CHECK-NOT: 'b' has incompatible definitions
extern int b;

// CHECK-NOT: 'c' has incompatible definitions
#if !__is_target_environment(macabi)
extern char c;
#endif

// CHECK-NOT: 'd' has incompatible definitions
#if __is_target_environment(macabi)
extern long d;
#endif

// CHECK-NOT: 'e' has incompatible definitions
// FIXME: Should we warn about this?
#if __is_target_environment(macabi)
extern int e;
#else
extern signed int e;
#endif
