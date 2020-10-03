// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-FULL
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -diag-depth 2 -verify -no-print %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-SHALLOW

typedef float F;
typedef int T;

#if !__is_target_environment(macabi)
// CHECK: diag.m:[[@LINE+2]]:11: warning: 'TT' has incompatible definitions
// CHECK: diag.m:[[@LINE+1]]:9: note: 'TT' is defined to type 'F' (aka 'float') here
typedef F TT;
#else
// CHECK: diag.m:[[@LINE+1]]:9: note: 'TT' is defined to type 'T' (aka 'int') here
typedef T TT;
#endif

// CHECK-NOT: 'MyInt' has incompatible definitions
#if !__is_target_environment(macabi)
typedef int MyInt;
#else
typedef T MyInt;
#endif

@interface Base @end
#if !__is_target_environment(macabi)
// CHECK: diag.m:[[@LINE+1]]:12: warning: 'Foo' has incompatible definitions
@interface Foo @end
#else
@interface Foo : Base @end
#endif

// CHECK: diag.m:[[@LINE+7]]:14: warning: 'FooRef' has incompatible definitions
// CHECK: note: for target x86_64-apple-macos10.15
// CHECK: diag.m:[[@LINE+5]]:9: note: 'FooRef' is defined to type 'Foo *' here
// CHECK: diag.m:[[@LINE-8]]:12: note: interface 'Foo' has no corresponding super class here
// CHECK: note: for target x86_64-apple-ios13.0-macabi
// CHECK: diag.m:[[@LINE+2]]:9: note: 'FooRef' is defined to type 'Foo *' here
// CHECK: diag.m:[[@LINE-9]]:18: note: interface 'Foo' has super class 'Base' here
typedef Foo *FooRef;

@interface Foo1
- (TT) bar;
@end

// CHECK: diag.m:[[@LINE+7]]:15: warning: 'Foo1Ref' has incompatible definitions
// CHECK: note: for target x86_64-apple-macos10.15
// CHECK-FULL: diag.m:[[@LINE+5]]:9: note: 'Foo1Ref' is defined to type 'Foo1 *' here
// CHECK-SHALLOW-NOT: diag.m:[[@LINE-6]]:9: note: 'Foo1Ref' is defined to type 'Foo1 *' here
// CHECK: diag.m:[[@LINE-7]]:4: note: return value has type 'TT' (aka 'float') here
// CHECK: note: for target x86_64-apple-ios13.0-macabi
// CHECK: diag.m:[[@LINE-9]]:4: note: return value has type 'TT' (aka 'int') here
typedef Foo1 *Foo1Ref;
