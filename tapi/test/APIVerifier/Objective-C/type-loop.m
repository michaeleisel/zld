// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s

@class B;

@interface A
-(B*) b;
@end

@interface B
-(A*) a;
@end

@class NSString;

@protocol NSObject
- (NSString *)description;
@end

@interface NSObject <NSObject>
- (NSString *)description;
@end

@interface NSString : NSObject
- (NSString *)description;
@end
