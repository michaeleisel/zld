//
//  VectorTests.m
//  UnitTests
//
//  Created by Louis Gerbarg on 12/28/20.
//

#import "DyldTestCase.h"


#include <vector>

#include "Vector.h"

using namespace dyld4;

@interface VectorTests : DyldTestCase {
    std::vector<uint64_t>           _testVector;
    Allocator                       _allocator;
    bool                            _initialized;
}

@end

@implementation VectorTests

- (void)setUp {
    if (_initialized) { return; }
    _initialized = true;
    uint64_t count = [self uniformRandomFrom:0 to:10000];
    for (auto i = 0; i < count; ++i) {
        _testVector.push_back([self uniformRandomFrom:0 to:10000]);
    }
}

- (void)tearDown {
    XCTAssert(_allocator.allocated_bytes() == 0);
}

- (void)checkVector:(const Vector<uint64_t>&)vec {
    XCTAssert(vec.size() == _testVector.size());
    for (auto i = 0; i < vec.size(); ++i) {
        XCTAssert(_testVector[i] == vec[i]);
    }
}

- (void)testPushBack {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.push_back(i);
    }
    [self checkVector:ints];
}

- (void)testEmplaceBack {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.emplace_back(i);
    }
    [self checkVector:ints];
}

- (void)testInsert {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    [self checkVector:ints];
}

- (void)testCopyConstructor {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    Vector<uint64_t> ints2(ints);
    [self checkVector:ints2];
}

- (void)testMoveConstructor {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    Vector<uint64_t> ints2(ints);
    [self checkVector:ints2];
}


- (void)testVectorInsertRValue {
    Vector<uint64_t> ints(&_allocator), ints2(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    for (auto& i : ints) {
        ints2.insert(ints2.cend(), std::move(i));
    }
    // The values in ints
    XCTAssertTrue(ints.size() == ints2.size());
    [self checkVector:ints2];
}

- (void)testCopyAssignment {
    Vector<uint64_t> ints(&_allocator), ints2(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    ints2 = ints;
    [self checkVector:ints2];
}

- (void) testVectorInsertRange {
    Vector<uint64_t> ints(&_allocator), ints2(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    ints2.insert(ints2.begin(), ints.begin(), ints.end());
    [self checkVector:ints2];
}

- (void) testVectorConstructRange {
    Vector<uint64_t> ints(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    Vector<uint64_t> ints2(ints.begin(), ints.end(), &_allocator);
    [self checkVector:ints2];
}

- (void) testVectorPushBackRValue {
    Vector<uint64_t> ints(&_allocator), ints2(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    for (auto& i : ints) {
        ints2.push_back(std::move(i));
    }
    [self checkVector:ints2];
}

- (void)testVectorMisc {
    Vector<uint64_t> ints(&_allocator), ints2(&_allocator);
    for (const auto& i : _testVector) {
        ints.insert(ints.end(), i);
    }
    ints2 = ints;
    ints.erase(ints.begin(), ints.end());
    XCTAssert(ints.size() == 0);
    ints = ints2;
    ints.erase(ints.cbegin(), ints.cend());
    XCTAssert(ints.size() == 0);
    ints = ints2;
    while(ints.size()) {
        ints.erase(ints.begin());
    }
    XCTAssert(ints.size() == 0);
    ints = ints2;
    while(ints.size()) {
        ints.erase(ints.cbegin());
    }
    XCTAssert(ints.size() == 0);
    ints = ints2;
    while(ints.size()) {
        ints.erase(ints.end()-1);
    }
    XCTAssert(ints.size() == 0);
    ints = ints2;
    while(ints.size()) {
        ints.erase(ints.cend()-1);
    }
    XCTAssert(ints.size() == 0);
    ints = std::move(ints2);
    while(ints.size()) {
        auto pos = ints.begin() + [self uniformRandomFrom:0 to:(ints.size()-1)];
        ints.erase(pos);
    }
}

// A struct with move semantics
struct MovedInteger
{
    MovedInteger() = default;
    MovedInteger(int v) : value(v) { }
    ~MovedInteger() = default;

    MovedInteger(const MovedInteger&) = delete;
    MovedInteger& operator=(const MovedInteger&) = delete;

    MovedInteger(MovedInteger&& other) {
        this->value = other.value;
        other.value = 0;
    }
    MovedInteger& operator=(MovedInteger&& other) {
        this->value = other.value;
        other.value = 0;
        return *this;
    }

    int value = 0;
};

-(void)testVectorEraseMovedElements {
    // Arrange: make a vector and remove an element
    Vector<MovedInteger> ints(&_allocator);
    ints.push_back(1);
    ints.push_back(2);
    ints.push_back(3);

    ints.erase(ints.begin());

    // Act: test the vector contents
    size_t size = ints.size();
    int e0 = ints[0].value;
    int e1 = ints[1].value;

    // Assert: vector is the correct size and the elements are as expected
    XCTAssertTrue(size == 2);
    XCTAssertTrue(e0 == 2);
    XCTAssertTrue(e1 == 3);
}

@end
