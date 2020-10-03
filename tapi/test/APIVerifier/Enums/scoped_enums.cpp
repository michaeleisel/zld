// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
namespace Foo {
// CHECK-NOT: scoped_enums.cpp:[[@LINE+1]]:6: warning: 'Foo::Bar' has incompatible definitions
enum Bar {
  A,
  B,
  C,
};
} // namespace Foo
#else
namespace Foo {
enum Bar {
  A,
  B,
  D,
};
}
#endif

#if !__is_target_environment(macabi)
namespace Baz {
// CHECK: scoped_enums.cpp:[[@LINE+1]]:6: warning: 'Baz::Bar' has incompatible definitions
enum Bar {
  A,
  B,
  // CHECK: scoped_enums.cpp:[[@LINE+1]]:3: note: enumerator 'C' with value 2 here
  C,
};
} // namespace Baz
#else
namespace Baz {
enum Bar {
  A,
  // CHECK: scoped_enums.cpp:[[@LINE+1]]:3: note: enumerator 'C' with value 1 here
  C,
};
}
#endif

#if !__is_target_environment(macabi)
// CHECK: scoped_enums.cpp:[[@LINE+1]]:6: warning: 'Bar' has incompatible definitions
enum Bar {
  A,
  // CHECK: scoped_enums.cpp:[[@LINE+1]]:3: note: enumerator 'B' with value 1 here
  B,
  C,
};
#else
enum Bar {
  // CHECK: scoped_enums.cpp:[[@LINE+1]]:3: note: enumerator 'B' with value 0 here
  B,
  C,
};
#endif
