// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck %s

typedef long NSInteger;

// CHECK:      - name: A
typedef enum __attribute__((enum_extensibility(open))) Foo : NSInteger Foo;
enum Foo : NSInteger {
  A,

// CHECK:      - name: B
  B,

// CHECK:      - name: C
  C,
};

enum __attribute__((enum_extensibility(open))) : NSInteger {
// CHECK:      - name: D
  D,

// CHECK:      - name: E
  E,

// CHECK:      - name: F
  F,
};
