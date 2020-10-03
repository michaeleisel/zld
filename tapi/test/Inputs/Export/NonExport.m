#import "Export.h"

__attribute__((visibility("hidden")))
@interface B
- (void) method;
@end

@implementation B
- (void) method {}
@end

@interface C : A
- (void) method;
@end

@implementation C : A
- (void) method {}
@end
