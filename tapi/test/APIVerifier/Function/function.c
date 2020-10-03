// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
// CHECK: function.c:[[@LINE+2]]:6: warning: 'check_return_type' has incompatible definitions
// CHECK: function.c:[[@LINE+1]]:1: note: return value has type 'void' here
void check_return_type(void);
#else
// CHECK: function.c:[[@LINE+1]]:1: note: return value has type 'int' here
int check_return_type(void);
#endif

#if !__is_target_environment(macabi)
// CHECK: function.c:[[@LINE+2]]:6: warning: 'check_parameter_type' has incompatible definitions
// CHECK: function.c:[[@LINE+1]]:32: note: parameter has type 'float' here
void check_parameter_type(int, float);
#else
// CHECK: function.c:[[@LINE+1]]:32: note: parameter has type 'double' here
void check_parameter_type(int, double);
#endif

#if !__is_target_environment(macabi)
// CHECK: function.c:[[@LINE+3]]:6: warning: 'check_missing_parameter' has incompatible definitions
// CHECK: function.c:[[@LINE+2]]:6: note: no corresponding parameter here
// CHECK: function.c:[[@LINE+3]]:35: note: parameter has type 'double' here
void check_missing_parameter(int);
#else
void check_missing_parameter(int, double);
#endif

#if !__is_target_environment(macabi)
// CHECK: function.c:[[@LINE+2]]:20: warning: 'check_inline' has incompatible definitions
// CHECK: function.c:[[@LINE+1]]:33: note: parameter has type 'int' here
static inline void check_inline(int a) {}
#else
// CHECK: function.c:[[@LINE+1]]:33: note: parameter has type 'float' here
static inline void check_inline(float);
void check_inline(float a) {}
#endif

// TODO: Check inline and attribute mismatches.
// #if __is_target_environment(macabi)
// static inline void check_not_inline() {}
// #else
// void check_not_inline();
// #endif
