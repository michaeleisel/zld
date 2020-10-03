// RUN: %tapi-frontend -target i386-apple-macos10.12 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -std=c++11 %s 2>&1 | FileCheck %s

// CHECK-LABEL: enum constants

// Forward declaration
enum Foo : int;

enum Bar {
  // CHECK:      - name: A
  A,

  // CHECK:      - name: B
  B,

  // CHECK:      - name: C
  C,
};

enum class Baz {
  // CHECK:      - name: Baz::A
  A,

  // CHECK:      - name: Baz::B
  B,

  // CHECK:      - name: Baz::C
  C,
};

enum struct Bazinga {
  // CHECK:      - name: Bazinga::A
  A,

  // CHECK:      - name: Bazinga::B
  B,

  // CHECK:      - name: Bazinga::C
  C,
};

enum {
  // CHECK:      - name: X
  X,

  // CHECK:      - name: Y
  Y,

  // CHECK:      - name: Z
  Z,
};

enum {
  // CHECK:      - name: U
  U,

  // CHECK:      - name: V
  V,

  // CHECK:      - name: W
  W,
};
