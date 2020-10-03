// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK:      - name: _foo
void foo(void);
// CHECK:      - name: _check_int
void check_int(int);
// CHECK:      - name: _check_return_int
int check_return_int(int);

// CHECK-NOT:      - name: _check_hidden
// CHECK:      - name: _check_default
void check_hidden(void) __attribute__((visibility("hidden")));
void check_default(void) __attribute__((visibility("default")));

// CHECK: check_inline
// CHECK: linkage: internal
static inline void check_inline() {}

// CHECK: check_inline1
// CHECK: linkage: internal
static inline void check_inline1();
void check_inline1() {}

// CHECK-NOT: check_static
static void check_static(void);
