// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck  %s

typedef long NSInteger;

// CHECK:      - name: A
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
typedef enum __attribute__((enum_extensibility(open))) Foo : NSInteger Foo;
enum Foo : NSInteger {
  A,

// CHECK:      - name: B
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.9 o:0 u:0
  B __attribute__((availability(macosx, introduced = 10.9))),

// CHECK:      - name: C
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.10 o:0 u:0
  C __attribute__((availability(macosx, introduced = 10.10))),
};

// CHECK-LABEL: objective-c interfaces:
// CHECK-NEXT: - name: Bar
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.9 o:0 u:0
// CHECK-NEXT:   categories:
// CHECK-NEXT:   protocols:
__attribute__((availability(macosx, introduced = 10.9)))
@interface Bar
// CHECK-NEXT:   methods:
// CHECK-NEXT:   - name: method1
// CHECK-NEXT:     kind: instance
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     isDynamic: false
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
- (void)method1;
// CHECK-NEXT:   - name: method2
// CHECK-NEXT:     kind: instance
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     isDynamic: false
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:10.10 o:0 u:0
- (void)method2 __attribute__((availability(macosx, introduced = 10.10)));
@end
