//
//  DyldTestCase.h
//  UnitTests
//
//  Created by Louis Gerbarg on 1/5/21.
//

#import <XCTest/XCTest.h>

#include <random>

NS_ASSUME_NONNULL_BEGIN

@interface DyldTestCase : XCTestCase {
    std::random_device  _rd;
    std::mt19937_64     _mt;
    uint64_t            _seed;
}

- (void) setRandomSeed:(uint64_t)seed;
- (bool) randomBool;
- (uint64_t) uniformRandomFrom:(uint64_t)lowerBound to:(uint64_t)upperBound;
@end

NS_ASSUME_NONNULL_END
