// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck %s

// CHECK:      objective-c interfaces
// CHECK-NEXT: - name: Foo
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.10 o:0 u:0
// CHECK-NEXT:   categories: NewStuff
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   properties:
// CHECK-NEXT:   instance variables:
__attribute__((availability(macosx, introduced = 10.10)))
@interface Foo
@end

// CHECK:        objective-c categories:
// CHECK-NEXT:   - name: NewStuff
// CHECK-NEXT:     interfaceName: Foo
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:10.11 o:0 u:0
// CHECK-NEXT:     protocols:
// CHECK-NEXT:     methods:
// CHECK-NEXT:     properties:
// CHECK-NEXT:     instance variables:
__attribute__((availability(macosx, introduced = 10.11)))
@interface Foo (NewStuff)
@end
