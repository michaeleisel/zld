// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
// CHECK: enums.c:[[@LINE+2]]:6: warning: 'enum Type' has incompatible definitions
// CHECK: enums.c:[[@LINE+1]]:6: note: enum has type 'int' here
enum Type {
  T1 = -1,
  T2 = 1,
};
#else
// CHECK: enums.c:[[@LINE+1]]:6: note: enum has type 'unsigned int' here
enum Type {
  T2 = 1
};
#endif

#if !__is_target_environment(macabi)
// CHECK: enums.c:[[@LINE+1]]:6: warning: 'enum Platform' has incompatible definitions
enum Platform {
  macOS = 1,
  iOS = 2,
  // CHECK: enums.c:[[@LINE+3]]:3: note: enumerator 'tvOS' with value 3 here
  // CHECK: enums.c:[[@LINE+11]]:3: note: enumerator 'tvOS' with value 4 here
  // CHECK: enums.c:[[@LINE+1]]:3: warning: 'tvOS' has incompatible definitions
  tvOS = 3,
  // CHECK: enums.c:[[@LINE+1]]:3: warning: 'watchOS' has incompatible definitions
  watchOS = 4,
};
#else
enum Platform {
  macOS = 1,
  iOS = 2,
  watchOS = 3,
  tvOS = 4,
};
#endif

#if !__is_target_environment(macabi)
// CHECK: enums.c:[[@LINE+1]]:6: warning: 'enum NotOrdered' has incompatible definitions
enum NotOrdered {
  Val1 = 1,
  // CHECK: enums.c:[[@LINE+3]]:3: note: enumerator 'Val2' with value 2 here
  // CHECK: enums.c:[[@LINE+12]]:3: note: enumerator 'Val2' with value 3 here
  // CHECK: enums.c:[[@LINE+1]]:3: warning: 'Val2' has incompatible definitions
  Val2 = 2,
  // CHECK: enums.c:[[@LINE+1]]:3: warning: 'Val3' has incompatible definitions
  Val3 = 3,
  Val4 = 4,
};
#else
enum NotOrdered {
  Val1 = 1,
  Val4 = 4,
  Val3 = 2,
  Val2 = 3,
};
#endif
// CHECK-NOT: warning: 'Val4' has incompatible definitions

// CHECK-NOT: warning: 'enum Missing' has incompatible definitions
enum Missing {
  Val5 = 5,
  Val6 = 6,
  Val7 = 7,
#if __is_target_environment(macabi)
  Val8 = 8,
#endif
};
