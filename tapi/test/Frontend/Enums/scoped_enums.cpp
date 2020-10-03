// RUN: %tapi-frontend -target i386-apple-macos10.12 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -std=c++11 %s 2>&1 | FileCheck %s

// CHECK-LABEL: enum constants

namespace Foo {
enum Bar {
  // CHECK:      - name: Foo::A
  A,

  // CHECK:      - name: Foo::B
  B,

  // CHECK:      - name: Foo::C
  C,
};
}

namespace Baz {
enum Bar {
  // CHECK:      - name: Baz::A
  A,

  // CHECK:      - name: Baz::B
  B,

  // CHECK:      - name: Baz::C
  C,
};
}

enum Bar {
  // CHECK:      - name: A
  A,

  // CHECK:      - name: B
  B,

  // CHECK:      - name: C
  C,
};
