// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

typedef long NSInteger;

#if !__is_target_environment(macabi)
// CHECK: enums.m:[[@LINE+1]]:72: warning: 'Foo' has incompatible definitions
typedef enum __attribute__((enum_extensibility(open))) Foo : NSInteger Foo;
// CHECK: enums.m:[[@LINE+1]]:6: warning: 'enum Foo' has incompatible definitions
enum Foo : NSInteger {
  A,
  B,
// CHECK: enums.m:[[@LINE+1]]:3: note: enumerator 'C' with value 2 here
  C,
};
#else
typedef enum __attribute__((enum_extensibility(open))) Foo : NSInteger Foo;
enum Foo : NSInteger {
  A,
// CHECK: enums.m:[[@LINE+1]]:3: note: enumerator 'C' with value 1 here
  C,
  D,
};
#endif

#if !__is_target_environment(macabi)
// CHECK: enums.m:[[@LINE+1]]:1: warning: 'enum (anonymous
enum __attribute__((enum_extensibility(open))) : NSInteger {
// CHECK: enums.m:[[@LINE+1]]:3: note: enumerator 'E' with value 1 here
  E = 1,
  F = 2,
};
#else
enum __attribute__((enum_extensibility(open))) : NSInteger {
// CHECK: enums.m:[[@LINE+1]]:3: note: enumerator 'E' with value 2 here
  E = 2,
  F = 1,
};
#endif

#if !__is_target_environment(macabi)
// CHECK: enums.m:[[@LINE+2]]:1: warning: 'enum (anonymous
// CHECK: enums.m:[[@LINE+1]]:50: note: enum has type 'int' here
enum __attribute__((enum_extensibility(open))) : int {
  G,
  H,
};
#else
// CHECK: enums.m:[[@LINE+1]]:50: note: enum has type 'NSInteger' (aka 'long') here
enum __attribute__((enum_extensibility(open))) : NSInteger {
  G,
  H,
};
#endif
