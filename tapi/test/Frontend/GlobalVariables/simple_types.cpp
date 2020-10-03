// RUN: %tapi-frontend -target i386-apple-macos10.12 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -std=c++11 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -std=c++11 %s 2>&1 | FileCheck %s

// 7.1.6.2 Simple type specifiers [dcl.type.simple]

// CHECK: globals:
extern bool b1;
// CHECK:      - name: _b1
extern char c1;
// CHECK:      - name: _c1
extern unsigned char c2;
// CHECK:      - name: _c2
extern signed char c3;
// CHECK:      - name: _c3
extern char16_t c4;
// CHECK:      - name: _c4
extern char32_t c5;
// CHECK:      - name: _c5
extern double d1;
// CHECK:      - name: _d1
extern long double d2;
// CHECK:      - name: _d2
extern float f1;
// CHECK:      - name: _f1
extern unsigned i1;
// CHECK:      - name: _i1
extern unsigned int i2;
// CHECK:      - name: _i2
extern signed i3;
// CHECK:      - name: _i3
extern signed int i4;
// CHECK:      - name: _i4
extern int i5;
// CHECK:      - name: _i5
extern unsigned long int l1;
// CHECK:      - name: _l1
extern unsigned long l2;
// CHECK:      - name: _l2
extern signed long int l3;
// CHECK:      - name: _l3
extern signed long l4;
// CHECK:      - name: _l4
extern long int l5;
// CHECK:      - name: _l5
extern long l6;
// CHECK:      - name: _l6
extern unsigned long long int ll1;
// CHECK:      - name: _ll1
extern unsigned long long ll2;
// CHECK:      - name: _ll2
extern signed long long int ll3;
// CHECK:      - name: _ll3
extern signed long long ll4;
// CHECK:      - name: _ll4
extern long long int ll5;
// CHECK:      - name: _ll5
extern long long ll6;
// CHECK:      - name: _ll6
extern unsigned short int s1;
// CHECK:      - name: _s1
extern unsigned short s2;
// CHECK:      - name: _s2
extern signed short int s3;
// CHECK:      - name: _s3
extern signed short s4;
// CHECK:      - name: _s4
extern short int s5;
// CHECK:      - name: _s5
extern short s6;
// CHECK:      - name: _s6
extern wchar_t w1;
// CHECK:      - name: _w1
