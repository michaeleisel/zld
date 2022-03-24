//
//  AllocatorTests.m
//  UnitTests
//
//  Created by Louis Gerbarg on 12/27/20.
//

#include <map>
#include <set>
#include <vector>

#import "DyldTestCase.h"
#include "Allocator.h"

using namespace dyld4;

struct AllocOperation {
    size_t size;
    size_t alignment;
    char pattern;
    AllocOperation(size_t S, size_t A, char P) : size(S), alignment(A), pattern(P) {}
};

struct TestOperation {
    AllocOperation* op;
    size_t          idx;
    bool            isAllocation;
    TestOperation(AllocOperation* O, size_t I, bool A) : op(O), idx(I), isAllocation(A) {}
};

@interface AllocatorTests : DyldTestCase {
    std::vector<AllocOperation> _testVector;
    std::vector<TestOperation>  _testOperations;
    size_t                      _testOperationMaxSize;
    size_t                      _smallAndMediumAllocationVectorMaxIndex;
    size_t                      _allocationVectorMaxIndex;
    Allocator                   _allocator;
}

@end

@implementation AllocatorTests

- (void)setUp {
    // FIrst build a set of small operations
    for (auto i = 0; i < 1024; ++i) {
        size_t size = [self uniformRandomFrom:0 to:1023];
        char pattern = [self uniformRandomFrom:0 to:255];
        auto alignment = 1 << [self uniformRandomFrom:4 to:7];
        _testVector.emplace_back(size, alignment, pattern);
    }
    _testVector.emplace_back(4096, PAGE_SIZE, [self uniformRandomFrom:0 to:255]);
    _testVector.emplace_back(16384, PAGE_SIZE, [self uniformRandomFrom:0 to:255]);
    _testVector.emplace_back(65536, PAGE_SIZE, [self uniformRandomFrom:0 to:255]);
    _smallAndMediumAllocationVectorMaxIndex = _testVector.size()-1;
    // Add a couple of elements that are large enough that they will be allocated up stream
    _testVector.emplace_back(1024*1024*2, 16, [self uniformRandomFrom:0 to:255]);
    _testVector.emplace_back(1024*1024*3, 16, [self uniformRandomFrom:0 to:255]);
    _allocationVectorMaxIndex = _testVector.size()-1;
}

- (void) buildAllocationOperationsTestVector:(bool)includeLargeOperations {
    size_t targetPoolSize = (includeLargeOperations ? (10*1024*1024) : (512*1024));
    uint64_t maxIndex = (includeLargeOperations ? _allocationVectorMaxIndex : _smallAndMediumAllocationVectorMaxIndex);
    uint64_t poolCycles = [self uniformRandomFrom:10 to:100];
    size_t spaceUsed = 0;
    std::vector<bool>           liveIdxes;
    std::vector<TestOperation>  liveAllocations;
    for (auto i = 0; i < poolCycles; ++i) {
        bool growing = true;
        while(1) {
            // First we grow the pool, so bias in favor of allocations
            bool isAllocation =  true;
            if (liveAllocations.size() != 0) {
                if (growing) {
                    isAllocation = ([self uniformRandomFrom:0 to:2] > 0) ? true : false;
                } else {
                    isAllocation = ([self uniformRandomFrom:0 to:2] > 0) ? false : true;
                }
            }
            if (isAllocation) {
                size_t allocationIndex = [self uniformRandomFrom:0 to:maxIndex];
                size_t targetAlignment = std::max(16UL, _testVector[allocationIndex].alignment);
                size_t targetSize = (_testVector[allocationIndex].size + (targetAlignment-1)) & (-1*targetAlignment);
                spaceUsed += targetSize;
                auto j = std::find(liveIdxes.begin(), liveIdxes.end(), false);
                if (j == liveIdxes.end()) {
                    j = liveIdxes.insert(liveIdxes.end(), true);
                } else {
                    *j = true;
                }
                auto op = TestOperation(&_testVector[allocationIndex], j-liveIdxes.begin(), true);
                _testOperations.push_back(op);
                liveAllocations.push_back(op);
            } else {
                size_t liveAllocationIndex = [self uniformRandomFrom:0 to:liveAllocations.size()-1];
                auto op = *(liveAllocations.begin() + liveAllocationIndex);
                size_t targetAlignment = std::max(16UL, _testVector[op.idx].alignment);
                size_t targetSize = (_testVector[op.idx].size + (targetAlignment-1)) & (-1*targetAlignment);
                spaceUsed -= targetSize;
                op.isAllocation = false;
                liveIdxes.begin()[op.idx] = false;
                _testOperations.push_back(op);
                liveAllocations.erase(liveAllocations.begin() + liveAllocationIndex);
            }
            if (growing && (spaceUsed >= targetPoolSize)) {
                growing = false;
            }
            if (!growing && (spaceUsed <= targetPoolSize/2)) {
                break;
            }
        }
    }
    _testOperationMaxSize = liveIdxes.size();
    // Drain the  pool
    while (liveAllocations.size()) {
        size_t liveAllocationIndex = [self uniformRandomFrom:0 to:liveAllocations.size()-1];
        auto op = *(liveAllocations.begin() + liveAllocationIndex);
        op.isAllocation = false;
        liveIdxes.begin()[op.idx] = false;
        _testOperations.push_back(op);
        liveAllocations.erase(liveAllocations.begin() + liveAllocationIndex);
    }
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)verifyBuffer:(Allocator::Buffer)buffer pattern:(uint8_t)pattern {
    const uint8_t* charBuffer = (uint8_t*)buffer.address;
    uint64_t patternBuffer = 0;
    // Hacky manually optimization, the XCTAssert macro defeats loop unrolling,
    // so we manually unroll this to speed up verification
    for (auto i = 0; i < 8; ++i) {
        patternBuffer <<= 8;
        patternBuffer |= pattern;
    }
    size_t i = 0;
    for (i = 0; i < buffer.size/8; ++i) {
        XCTAssert(*(uint64_t*)&charBuffer[i] == patternBuffer, "failed 0x%lx[%zu] == %u\n", (uintptr_t)charBuffer, i, (unsigned char)pattern);
    }
    for (; i < buffer.size; ++i) {
        XCTAssert(charBuffer[i] == pattern, "failed 0x%lx[%zu] == %u\n", (uintptr_t)charBuffer, i, (unsigned char)pattern);
    }
}

- (void) runRandomAllocatorTests:(Allocator&)allocator verify:(bool)verify {
    auto liveAllocations = (std::pair<Allocator::Buffer,AllocOperation*>*)malloc(sizeof(std::pair<Allocator::Buffer,uint8_t>)*_testOperationMaxSize);
    bzero((void*)liveAllocations, sizeof(std::pair<Allocator::Buffer,AllocOperation*>)*_testOperationMaxSize);

    uint64_t count = 0;
    for (auto op : _testOperations) {
        if (op.isAllocation) {
            auto buffer = allocator.allocate_buffer(op.op->size, op.op->alignment);
            allocator.validateFreeList();
//            printf("Allocated(%lu) 0x%lx\n", op.idx, (uintptr_t)buffer.address);
//            printf("\tsize: %zu, align = %zu, pattern: %u \n", op.op->size, op.op->alignment, (uint8_t)op.op->pattern);
            liveAllocations[op.idx].first = buffer;
            if (verify) {
                memset(buffer.address, op.op->pattern, buffer.size);
                liveAllocations[op.idx].second = op.op;
            }
        } else {
            allocator.deallocate_buffer(liveAllocations[op.idx].first);
            allocator.validateFreeList();
//            printf("\tsize: %zu, align = %zu, pattern: %u \n", op.op->size, op.op->alignment, (uint8_t)op.op->pattern);
            liveAllocations[op.idx] = { Allocator::Buffer(), (AllocOperation*)nullptr };
        }

        // We only check every 100th iteration in order to speed up test runs. If crashes happen change this to validate every
        // modification
        ++count;
        if (verify && (count%100 == 0)) {
            for (auto j = &liveAllocations[0]; j != &liveAllocations[_testOperationMaxSize]; ++j) {
                if (!j->first.valid()) { continue; }
                [self verifyBuffer:j->first pattern:j->second->pattern];
            }
        }
    }
    free((void*)liveAllocations);
    XCTAssert(allocator.allocated_bytes() == 0, "allocator.allocated_bytes() = %zu\n", allocator.allocated_bytes());
}

- (void) runRandomMallocTests:(Allocator&)allocator verify:(bool)verify {
    auto liveAllocations = (std::pair<void*,AllocOperation*>*)malloc(sizeof(std::pair<void*,uint8_t>)*_testOperationMaxSize);
    bzero((void*)liveAllocations, sizeof(std::pair<void*,AllocOperation*>)*_testOperationMaxSize);

    uint64_t count = 0;
    for (auto op : _testOperations) {
        if (op.isAllocation) {
            auto buffer = allocator.aligned_alloc(op.op->alignment, op.op->size);
            allocator.validateFreeList();
//            printf("Allocated(%lu) 0x%lx\n", op.idx, (uintptr_t)buffer.address);
//            printf("\tsize: %zu, align = %zu, pattern: %u \n", op.op->size, op.op->alignment, (uint8_t)op.op->pattern);
            liveAllocations[op.idx].first = buffer;
            if (verify) {
                memset(buffer, op.op->pattern, op.op->size);
                liveAllocations[op.idx].second = op.op;
            }
        } else {
            allocator.free(liveAllocations[op.idx].first);
            allocator.validateFreeList();
//            printf("\tsize: %zu, align = %zu, pattern: %u \n", op.op->size, op.op->alignment, (uint8_t)op.op->pattern);
            liveAllocations[op.idx] = { nullptr, (AllocOperation*)nullptr };
        }
        
        // We only check every 100th iteration in order to speed up test runs. If crashes happen change this to validate every
        // modification
        ++count;
        if (verify && (count%100 == 0)) {
            for (auto j = &liveAllocations[0]; j != &liveAllocations[_testOperationMaxSize]; ++j) {
                if (j->first == nullptr) { continue; }
                [self verifyBuffer:{j->first, j->second->size} pattern:j->second->pattern];
            }
        }
    }
    free((void*)liveAllocations);
    XCTAssert(allocator.allocated_bytes() == 0, "allocator.allocated_bytes() = %zu\n", allocator.allocated_bytes());
}

- (void) testMalloc {
    auto allocator = Allocator();
    [self buildAllocationOperationsTestVector:false];
    [self runRandomMallocTests:allocator verify:true];
}

- (void)testAlloactor {
    auto allocator = Allocator();
    [self buildAllocationOperationsTestVector:false];
    [self runRandomAllocatorTests:allocator verify:true];
}

static bool sDestructorCalledAtLeastOnce = false;

struct TestStruct {
    TestStruct() = default;
    TestStruct(uint32_t A, uint32_t B, uint32_t C, uint32_t D) :  a(A), b(B), c(C), d(D) {}
    ~TestStruct() { sDestructorCalledAtLeastOnce = true; }
    uint32_t a = 0;
    uint32_t b = 1;
    uint32_t c = 2;
    uint32_t d = 3;
};

- (void) testUniquePtr {
    sDestructorCalledAtLeastOnce = false;
    auto allocator = Allocator();
    {
        UniquePtr<TestStruct> purposefullyUnusedToTestNullHandling;
        auto test1 = allocator.makeUnique<TestStruct>();
        auto test2 = allocator.makeUnique<TestStruct>(4, 5, 6, 7);
        XCTAssertEqual(test1->a, 0);
        XCTAssertEqual(test1->b, 1);
        XCTAssertEqual(test1->c, 2);
        XCTAssertEqual(test1->d, 3);
        XCTAssertEqual(test2->a, 4);
        XCTAssertEqual(test2->b, 5);
        XCTAssertEqual(test2->c, 6);
        XCTAssertEqual(test2->d, 7);

        test1->a = 8;
        test1->b = 9;
        test1->c = 10;
        test1->d = 11;

        XCTAssertEqual(test1->a, 8);
        XCTAssertEqual(test1->b, 9);
        XCTAssertEqual(test1->c, 10);
        XCTAssertEqual(test1->d, 11);

        UniquePtr<TestStruct> test0;
        test0 = std::move(test1);

        XCTAssert((bool)test1 == false);
        XCTAssertEqual(test0->a, 8);
        XCTAssertEqual(test0->b, 9);
        XCTAssertEqual(test0->c, 10);
        XCTAssertEqual(test0->d, 11);
        test2 = allocator.makeUnique<TestStruct>();
        XCTAssertTrue(sDestructorCalledAtLeastOnce);
        sDestructorCalledAtLeastOnce = false;
    }
    XCTAssertTrue(sDestructorCalledAtLeastOnce);
}

- (void) testSharedPtr {
    sDestructorCalledAtLeastOnce = false;
    auto allocator = Allocator();
    {
        SharedPtr<TestStruct> purposefullyUnusedToTestNullHandling;
        auto test1 = allocator.makeShared<TestStruct>();
        auto test2 = allocator.makeShared<TestStruct>(4, 5, 6, 7);
        XCTAssertEqual(test1->a, 0);
        XCTAssertEqual(test1->b, 1);
        XCTAssertEqual(test1->c, 2);
        XCTAssertEqual(test1->d, 3);
        XCTAssertEqual(test2->a, 4);
        XCTAssertEqual(test2->b, 5);
        XCTAssertEqual(test2->c, 6);
        XCTAssertEqual(test2->d, 7);

        test1->a = 8;
        test1->b = 9;
        test1->c = 10;
        test1->d = 11;

        XCTAssertEqual(test1->a, 8);
        XCTAssertEqual(test1->b, 9);
        XCTAssertEqual(test1->c, 10);
        XCTAssertEqual(test1->d, 11);

        SharedPtr<TestStruct> test0;
        test0 = std::move(test1);

        XCTAssert((bool)test1 == false);
        XCTAssertEqual(test0->a, 8);
        XCTAssertEqual(test0->b, 9);
        XCTAssertEqual(test0->c, 10);
        XCTAssertEqual(test0->d, 11);

        test1 = test0;

        XCTAssertEqual(test1->a, 8);
        XCTAssertEqual(test1->b, 9);
        XCTAssertEqual(test1->c, 10);
        XCTAssertEqual(test1->d, 11);

        XCTAssertEqual(test1.get(), test0.get());
        test2 = allocator.makeShared<TestStruct>();
        XCTAssertTrue(sDestructorCalledAtLeastOnce);
        sDestructorCalledAtLeastOnce = false;
    }
    XCTAssertTrue(sDestructorCalledAtLeastOnce);
}

- (void) testAllocatorNullHandling {
    // There are no XCTAsserts here, if test does not crash it passed
    auto allocator = Allocator();
    auto nullUnique = allocator.makeUnique<char *>(nullptr);
    auto nullShared = allocator.makeShared<char *>(nullptr);
    staticFree(nullptr);
}

//FIXME: We need mach exception handling for this test to make sense
- (void) disabledTestWriteProtect {
    auto allocator = Allocator();
    auto buffer = allocator.allocate_buffer(128, 16);
    memset(buffer.address, 0x1f, buffer.size);
    allocator.writeProtect(true);
    [self verifyBuffer:buffer pattern:0x1f];

    // FIXME: Implement mach exception handling on this to test write protection and make sure we do not regress it
    //    memset(buffer.address, 0x2e, buffer.size);
    allocator.writeProtect(false);
    allocator.deallocate_bytes(buffer.address, buffer.size, 16);
    XCTAssert(allocator.allocated_bytes() == 0, "allocator.allocated_bytes() = %zu\n", allocator.allocated_bytes());
}

- (void) testAllocatorPerformance {
    Allocator localAllocator;
    __block auto& allocator = localAllocator;
    [self buildAllocationOperationsTestVector:false];
    [self measureBlock:^{
        [self runRandomAllocatorTests:allocator verify:false];
    }];
}

- (void) testMallocPerformance {
    Allocator localAllocator;
    __block auto& allocator = localAllocator;
     [self buildAllocationOperationsTestVector:false];
    [self measureBlock:^{
        [self runRandomMallocTests:allocator verify:false];
    }];
}

@end
