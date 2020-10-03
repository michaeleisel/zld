// RUN: %tapi-frontend -target x86_64-apple-macos10.12 -v %s 2>&1 | FileCheck %s

// CHECK: clang Invocation:
// CHECK: "-triple" "x86_64-apple-macosx10.12.0"
