// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -std=c++11 -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
// CHECK: simple_types.cpp:[[@LINE+2]]:13: warning: 'foo' has incompatible definitions
// CHECK: simple_types.cpp:[[@LINE+1]]:9: note: 'foo' is defined to type 'int' here
typedef int foo;
#else
// CHECK: simple_types.cpp:[[@LINE+1]]:9: note: 'foo' is defined to type 'short' here
typedef short foo;
#endif

// CHECK: simple_types.cpp:[[@LINE+3]]:13: warning: 'foobar' has incompatible definitions
// CHECK: simple_types.cpp:[[@LINE+2]]:9: note: 'foobar' is defined to type 'foo' (aka 'int') here
// CHECK: simple_types.cpp:[[@LINE+1]]:9: note: 'foobar' is defined to type 'foo' (aka 'short') here
typedef foo foobar;

// CHECK: simple_types.cpp:[[@LINE+3]]:12: warning: 'a' has incompatible definitions
// CHECK: simple_types.cpp:[[@LINE+2]]:8: note: variable 'a' has type 'foo' (aka 'int') here
// CHECK: simple_types.cpp:[[@LINE+1]]:8: note: variable 'a' has type 'foo' (aka 'short') here
extern foo a;

// CHECK-NOT: 'bar' has incompatible definitions
typedef int bar;

// CHECK-NOT: 'letter' has incompatible definitions
#if !__is_target_environment(macabi)
typedef char letter;
#endif

// CHECK-NOT: 'SOME_TYPE' has incompatible definitions
#if __is_target_environment(macabi)
typedef long SOME_TYPE;
#endif

// CHECK-NOT: 'INTERESTING_TYPE' has incompatible definitions
// FIXME: Should we warn about this?
#if __is_target_environment(macabi)
typedef int INTERESTING_TYPE;
#else
typedef signed int INTERESTING_TYPE;
#endif

#if !__is_target_environment(macabi)
// CHECK: simple_types.cpp:[[@LINE+3]]:25: warning: 'MyTypeRef' has incompatible definitions
// CHECK: simple_types.cpp:[[@LINE+2]]:9: note: 'MyTypeRef' is defined to type 'struct opaque1 *' here
// CHECK: simple_types.cpp:[[@LINE+1]]:16: note: declaration has name 'opaque1' here
typedef struct opaque1 *MyTypeRef;
#else
// CHECK: simple_types.cpp:[[@LINE+2]]:9: note: 'MyTypeRef' is defined to type 'struct opaque2 *' here
// CHECK: simple_types.cpp:[[@LINE+1]]:16: note: declaration has name 'opaque2' here
typedef struct opaque2 *MyTypeRef;
#endif
