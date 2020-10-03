// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK-LABEL: globals:

// CHECK-NEXT: - name: __Z3fooi
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   access: public
// CHECK-NEXT:   isWeakDefined: false
// CHECK-NEXT:   isThreadLocalValue: false
// CHECK-NEXT:   kind: function
// CHECK-NEXT:   linkage: internal
inline int foo(int x) { return x + 1; }

// CHECK-NEXT: - name: __Z3bari
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   access: public
// CHECK-NEXT:   isWeakDefined: false
// CHECK-NEXT:   isThreadLocalValue: false
// CHECK-NEXT:   kind: function
// CHECK-NEXT:   linkage: exported
extern int bar(int x) { return x + 1; }

// CHECK-NEXT: - name: __Z3bazi
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   access: public
// CHECK-NEXT:   isWeakDefined: false
// CHECK-NEXT:   isThreadLocalValue: false
// CHECK-NEXT:   kind: function
// CHECK-NEXT:   linkage: internal
inline int baz(int x) {
  static const int a[] = {1, 2, 3};
  return a[x];
}
