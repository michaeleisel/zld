// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_I386 %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s

// CHECK-LABEL: objective-c interfaces:
// CHECK-NEXT: - name: Forward
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   categories:
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   properties:
@interface Forward {
// CHECK-NEXT:   instance variables:
// CHECK-NEXT:   - name: ivar1
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: public
@public
  int ivar1;
// CHECK:        - name: ivar2
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: protected
@protected
  int ivar2;
// CHECK:        - name: ivar3
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: package
@package
  int ivar3;
// CHECK:        - name: ivar4
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: private
@private
  int ivar4;
}
@end

// CHECK:      - name: Reverse
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   categories:
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   properties:
@interface Reverse {
// CHECK-NEXT:   instance variables:
// CHECK-NEXT:   - name: ivar4
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: private
@private
  int ivar4;
// CHECK:        - name: ivar3
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: package
@package
  int ivar3;
// CHECK:        - name: ivar2
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: protected
@protected
  int ivar2;
// CHECK:        - name: ivar1
// CHECK-NEXT:     loc:
// CHECK-NEXT:     access: public
@public
  int ivar1;
}
@end
