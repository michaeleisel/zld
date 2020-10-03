// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macamaccatalystbi -verify -verifier-diag-style=silent -no-print %s 2>&1 | FileCheck %s --check-prefix=SILENT --allow-empty
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -verifier-diag-style=error -no-print %s 2>&1 | FileCheck %s --check-prefix=ERROR --allow-empty

#if !__is_target_environment(macabi)
@class NSView;
// CHECK: interface.m:[[@LINE+2]]:16: warning: 'a' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:8: note: variable 'a' has type 'NSView *' here
extern NSView *a;
#else
@class UIView;
// CHECK: interface.m:[[@LINE+1]]:8: note: variable 'a' has type 'UIView *' here
extern UIView *a;
#endif

// SILENT-NOT: warning:
// SILENT-NOT: error:
// SILENT-NOT: note:
// ERROR: error: 'a' has incompatible definitions
