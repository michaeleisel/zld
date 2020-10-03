// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -Xparser -I%S -verify -no-print %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-ALL
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -Xparser -I%S -verify -no-print --skip-external-headers %s 2>&1 | FileCheck %s --check-prefix=CHECK --check-prefix=CHECK-SKIP

#include "Inputs/external.h"

// CHECK-ALL: warning: 'foo' has incompatible definitions
// CHECK-SKIP-NOT: warning: 'foo' has incompatible definitions
MyType foo(void);

// CHECK: warning: 'foo1' has incompatible definitions
#if !__is_target_environment(macabi)
int foo1(void);
#else
MyType foo1(void);
#endif

// CHECK: warning: 'foo2' has incompatible definitions
#if !__is_target_environment(macabi)
MyType foo2(void);
#else
float foo2(void);
#endif

// CHECK: warning: 'foo3' has incompatible definitions
#if !__is_target_environment(macabi)
MyType foo3(void);
#else
MyType2 foo3(void);
#endif

// CHECK-ALL: warning: 'CheckType' has incompatible definitions
// CHECK-ALL: warning: 'CheckType2' has incompatible definitions
// CHECK-SKIP-NOT: warning: 'CheckType' has incompatible definitions
// CHECK-SKIP-NOT: warning: 'CheckType2' has incompatible definitions
typedef A CheckType;
typedef MyType CheckType2;

// CHECK-ALL: warning: 'B' has incompatible definitions
// CHECK-SKIP-NOT: warning: 'B' has incompatible definitions
@interface B : CheckType
- (CheckType*) val;
- (CheckType2*) val2;
@end

#if !__is_target_environment(macabi)
typedef MyType CheckType3;
#else
typedef MyType2 CheckType3;
#endif

// CHECK: warning: 'C' has incompatible definitions
@interface C
- (CheckType3*) val;
@end

// CHECK-SKIP-NOT: warning: 'my_function' has incompatible definitions
void my_function(MyStruct a);
