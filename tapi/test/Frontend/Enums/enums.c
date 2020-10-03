// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK-LABEL: enum constants

// Forward declaration
enum Foo;

enum Bar {
  // CHECK:      - name: A
  A,

  // CHECK:      - name: B
  B,

  // CHECK:      - name: C
  C,
};

enum Baz {
  // CHECK:      - name: D
  D = 10,

  // CHECK:      - name: E
  E = 0,

  // CHECK:      - name: F
  F = -1,
};

enum {
  // CHECK:      - name: G
  G = 10,

  // CHECK:      - name: H
  H = 0,
};
