// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -Xparser -std=c++14 -verify -no-print %s 2>&1 | FileCheck %s

// Check C++ enum class works properly.

enum class Foo {
  // CHECK-NOT: enums.cpp:[[@LINE+1]]:3: warning: 'Foo' has incompatible definitions
  A,
  B
};

#if !__is_target_environment(macabi)
enum class Bar {
  // CHECK: enums.cpp:[[@LINE+1]]:3: warning: 'A' has incompatible definitions
  A,
  // CHECK: enums.cpp:[[@LINE+1]]:3: warning: 'B' has incompatible definitions
  B
};
#else
enum class Bar {
  B,
  A
};
#endif
