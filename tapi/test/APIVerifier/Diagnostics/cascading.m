// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=CASCADE
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print -no-cascading-diagnostics %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=NO-CASCADE
// CASCADE: warning: 'enum Type' has incompatible definitions
// NO-CASCADE-NOT: warning: 'enum Type' has incompatible definitions
// CHECK: warning: 'T1' has incompatible definitions
// CHECK: warning: 'T2' has incompatible definitions
// CASCADE: warning: 'MyType' has incompatible definitions
// CASCADE: warning: 'foo' has incompatible definitions
// NO-CASCADE-NOT: warning: 'MyType' has incompatible definitions
// NO-CASCADE-NOT: warning: 'foo' has incompatible definitions

typedef enum Type {
#if !__is_target_environment(macabi)
  T1 = 1,
  T2 = 2,
#else
  T1 = 2,
  T2 = 1,
#endif
} MyType;

MyType foo();
