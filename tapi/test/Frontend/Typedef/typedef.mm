// RUN: %tapi-frontend -target i386-apple-macos10.15 %s 2>&1 | FileCheck --check-prefixes CHECK,CHECK-MAC %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck --check-prefixes CHECK,CHECK-MAC %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck --check-prefixes CHECK,CHECK-IOSMAC %s

// 7.1.3 The typedef specifier

// CHECK-LABEL: type defs:
// CHECK:      - name: A
typedef unsigned A;

// CHECK:      - name: B
// CHECK:      - name: C
typedef int B, *C;

// CHECK:      - name: D
using D = void (*)(int);

// CHECK:      - name: E
typedef struct E { /* ... */ } E;

// CHECK:       - name: new_type
// CHECK-MAC:     availability: i:10.12 o:0 u:0
// CHECK-IOSMAC:  availability: i:13 o:0 u:0
typedef int new_type __attribute__((availability(macos, introduced = 10.12)))
                     __attribute__((availability(ios, introduced = 13.0)));

// CHECK:      - name: G
// CHECK:      - name: H
struct F {
  typedef struct G { } G;
  typedef struct H H;
};

// CHECK:      - name: I
// CHECK:      - name: J
typedef struct { } *I, J;

@class K;

@protocol L - (void) foo; @end

@protocol M <L> @end

@interface N - (void) foo; @end

// CHECK:      - name: O
// CHECK:      - name: O1
// CHECK:      - name: O2
typedef K O;
typedef N O1;
typedef N <M> O2;

class P;
template<typename T> class P1 {};

// CHECK:      - name: Q
// CHECK:      - name: Q1
// CHECK:      - name: Q2

typedef P Q;
typedef P1<int> Q1;
// FIXME: How to record this?
template<typename T> using Q2 = P1<T>;

// CHECK-NOT:  - name: S
// CHECK-NOT:  - name: T
@class R<S, T : N *>;

// CHECK-NOT:  - name: local_int
void test() {
  typedef int local_int;
}

// CHECK-NOT: - name: N<L>
@interface test1
- (void) run: (N<L> *) param;
@end

// CHECK-LABEL: globals:
