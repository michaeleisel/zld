// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -std=c++11 -verify -no-print %s 2>&1 | FileCheck %s

// CHECK: record_types.cpp:[[@LINE+9]]:3: warning: 'MyStruct' has incompatible definitions
typedef struct MyStruct {
#if !__is_target_environment(macabi)
  // CHECK: record_types.cpp:[[@LINE+1]]:7: note: field has name 'verison' here
  int verison;
#else
  // CHECK: record_types.cpp:[[@LINE+1]]:7: note: field has name 'maccatalyst_verison' here
  int maccatalyst_verison;
#endif
} MyStruct;

// CHECK: record_types.cpp:[[@LINE+9]]:3: warning: 'MyStruct2' has incompatible definitions
typedef struct MyStruct2 {
#if !__is_target_environment(macabi)
  // CHECK: record_types.cpp:[[@LINE+1]]:7: note: field 'foo' has type 'int' here
  int foo;
#else
  // CHECK: record_types.cpp:[[@LINE+1]]:9: note: field 'foo' has type 'float' here
  float foo;
#endif
} MyStruct2;

// CHECK: record_types.cpp:[[@LINE+9]]:3: warning: 'MyStruct3' has incompatible definitions
typedef struct MyStruct3 {
#if !__is_target_environment(macabi)
  // CHECK: record_types.cpp:[[@LINE+1]]:12: note: field 'foo' is not bit field here
  unsigned foo;
#else
  // CHECK: record_types.cpp:[[@LINE+1]]:12: note: field 'foo' is bit field here
  unsigned foo : 4;
#endif
} MyStruct3;

// CHECK: record_types.cpp:[[@LINE+9]]:3: warning: 'MyStruct4' has incompatible definitions
typedef struct MyStruct4 {
#if !__is_target_environment(macabi)
  // CHECK: record_types.cpp:[[@LINE+1]]:12: note: bit field 'foo' has width 4 here
  unsigned foo : 4;
#else
  // CHECK: record_types.cpp:[[@LINE+1]]:12: note: bit field 'foo' has width 8 here
  unsigned foo : 8;
#endif
} MyStruct4;

// CHECK: record_types.cpp:[[@LINE+7]]:3: warning: 'MyStruct5' has incompatible definitions
typedef struct MyStruct5 {
#if !__is_target_environment(macabi)
  // CHECK: record_types.cpp:[[@LINE+1]]:12: note: 'MyStruct5' has field 'foo' here
  unsigned foo;
// CHECK: record_types.cpp:[[@LINE-4]]:16: note: 'MyStruct5' has no corresponding field here
#endif
} MyStruct5;

// Inheritance.
typedef struct Base {
} Base;
typedef struct Base2 {
} Base2;

#if !__is_target_environment(macabi)
// CHECK: record_types.cpp:[[@LINE+7]]:3: warning: 'MyStruct6' has incompatible definitions
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: 'MyStruct6' has base class 'Base' here
typedef struct MyStruct6 : public Base {
#else
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: 'MyStruct6' has base class 'Base2' here
typedef struct MyStruct6 : public Base2{
#endif
} MyStruct6;

#if !__is_target_environment(macabi)
// CHECK: record_types.cpp:[[@LINE+7]]:3: warning: 'MyStruct7' has incompatible definitions
// CHECK: record_types.cpp:[[@LINE+1]]:9: note: 'MyStruct7' has 1 base class(es) here
typedef struct MyStruct7 : public Base {
#else
// CHECK: record_types.cpp:[[@LINE+1]]:9: note: 'MyStruct7' has 2 base class(es) here
typedef struct MyStruct7 : public Base, Base2{
#endif
} MyStruct7;

#if !__is_target_environment(macabi)
// CHECK: record_types.cpp:[[@LINE+7]]:3: warning: 'MyStruct8' has incompatible definitions
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: base class 'Base' is virtual here
typedef struct MyStruct8 : public virtual Base {
#else
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: base class 'Base' is not virtual here
typedef struct MyStruct8 : public Base{
#endif
} MyStruct8;

#if !__is_target_environment(macabi)
// CHECK: record_types.cpp:[[@LINE+7]]:3: warning: 'MyStruct9' has incompatible definitions
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: base class 'Base' has access specifier 'protected' here
typedef struct MyStruct9 : protected Base {
#else
// CHECK: record_types.cpp:[[@LINE+1]]:28: note: base class 'Base' has access specifier 'public' here
typedef struct MyStruct9 : public Base{
#endif
} MyStruct9;

// Anonymous union/struct
// CHECK: record_types.cpp:[[@LINE+12]]:3: warning: 'MyStruct10' has incompatible definitions
typedef struct MyStruct10 {
  union {
    int x;
#if !__is_target_environment(macabi)
    // CHECK: record_types.cpp:[[@LINE+4]]:11: note: 'MyStruct10::(anonymous union at
    // CHECK-SAME: has field 'y' here
    // CHECK: record_types.cpp:[[@LINE-5]]:3: note: 'MyStruct10::(anonymous union at
    // CHECK-SAME: has no corresponding field here
    float y;
#endif
  };
} MyStruct10;
