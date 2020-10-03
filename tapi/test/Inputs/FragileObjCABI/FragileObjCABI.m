#import "FragileObjCABI.h"

@implementation PublicClass
@end

__attribute__((visibility("hidden"))) @interface HiddenClass
    : NSObject @end

      @implementation HiddenClass @end
