//
//  DyldTestCase.m
//  UnitTests
//
//  Created by Louis Gerbarg on 1/5/21.
//

#import "DyldTestCase.h"

@implementation DyldTestCase

- (instancetype)initWithInvocation:(nullable NSInvocation *)invocation {
    self = [super initWithInvocation:invocation];
    
    if (self) {
        self.continueAfterFailure = false;
    }
    
    return self;
}

- (void)recordIssue:(XCTIssue *)issue {
    XCTMutableIssue* newIssue = [issue mutableCopy];
    newIssue.compactDescription = [NSString stringWithFormat:@"%@ (randomSeed: %llu)", issue.compactDescription, _seed];
    [super recordIssue:newIssue];
}

- (void)performTest:(XCTestRun *)run  {
    std::uniform_int_distribution<uint64_t> dist(0,std::numeric_limits<uint64_t>::max());
    _seed = dist(_rd);
    [super performTest:run];
}

- (void) setRandomSeed:(uint64_t)seed {
    _seed = seed;
    _mt.seed(_seed);
}


- (bool) randomBool {
    std::uniform_int_distribution<bool> dist(0, 1);
    return dist(_mt);
}

- (uint64_t) uniformRandomFrom:(uint64_t)lowerBound to:(uint64_t)upperBound {
    std::uniform_int_distribution<uint64_t> dist(lowerBound, upperBound);
    return dist(_mt);
}

@end
