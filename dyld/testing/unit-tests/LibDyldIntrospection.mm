//
//  LibDyldIntrospection.m
//  UnitTests
//
//  Created by Louis Gerbarg on 1/28/21.
//

#import <XCTest/XCTest.h>

#include <TargetConditionals.h>
#include <dirent.h>
#include <mach-o/loader.h>
#include "dyld_introspection.h"
#include "Vector.h"

#include "DyldTestCase.h"

@interface LibDyldIntrospection : DyldTestCase {
    uint32_t _openFDCount;
}
@end

@implementation LibDyldIntrospection

- (uint32_t) countOpenFiles {
    uint32_t fileCount = 0;
    dirent entry;
    dirent* entp = NULL;
    DIR* dirp = opendir("/dev/fd");
    assert(dirp != NULL);
    while ( ::readdir_r(dirp, &entry, &entp) == 0 ) {
        if ( entp == NULL )
            break;
        // Open file descriptos are DT_UNKNOWN
        if (entp->d_type == DT_UNKNOWN) {
            fileCount++;
        }
    }
    closedir(dirp);
    return fileCount;
}

- (void) setUp {
    _openFDCount = [self countOpenFiles];
}

- (void) tearDown {
    //Ensure that we do not leak file descirptors
    XCTAssertEqual(_openFDCount, [self countOpenFiles]);
}

// TODO: Remove this once we no longer buuld on pre-macOS 12.0 aligned platforms.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

- (void) validateCache:(dyld_shared_cache_t)cache log:(bool)log {
    XCTAssertNotNil((id)cache);
    uint64_t baseAddress = dyld_shared_cache_get_base_address(cache);
    uint64_t size = dyld_shared_cache_get_mapped_size(cache);
    if (log) printf("Found cache (0x%llx - 0x%llx)\n", baseAddress, baseAddress+size);
    dyld_shared_cache_for_each_file(cache, ^(const char *file_path) {
        if (log) printf("\tFile: %s\n", file_path);
    });
    dyld_shared_cache_for_each_image(cache, ^(dyld_image_t image) {
        if (log) printf("\tImage: %s\n", dyld_image_get_installname(image));
        if (log) printf("\tSegments:\n");
        __block uint64_t textAddr = 0;
        __block uint64_t textSize = 0;
        dyld_image_for_each_segment_info(image, ^(const char *segmentName, uint64_t vmAddr, uint64_t vmSize, int perm) {
            if (strcmp(segmentName, "__TEXT") == 0) {
                textAddr = vmAddr;
                textSize = vmSize;
                XCTAssertEqual(perm, (PROT_EXEC | PROT_READ));
            }
            if (log) printf("\t\t%s: (0x%llx - 0x%llx)\n", segmentName, vmAddr, vmAddr+vmSize);
        });
        __block bool didRunTEXTCallback = false;
        XCTAssertTrue(dyld_image_content_for_segment(image, "__TEXT", ^(const void *content, uint64_t vmAddr, uint64_t vmSize) {
            didRunTEXTCallback = true;
            auto mh = (const mach_header*)content;
            XCTAssert(mh->magic == MH_MAGIC_64 || mh->magic == MH_MAGIC);
            XCTAssertEqual(vmAddr, textAddr);
            XCTAssertEqual(vmSize, textSize);
        }));
        XCTAssertTrue(didRunTEXTCallback);

        __block bool didRunDATACallback = false;
        bool foundDATASegment = dyld_image_content_for_segment(image, "__DATA", ^(const void *content, uint64_t vmAddr, uint64_t vmSize) {
            //TODO: We should validate the __DATA content somehow
            didRunDATACallback = true;
        });
        XCTAssertEqual(foundDATASegment, didRunDATACallback);
        if (log) printf("\tSections:\n");
        dyld_image_for_each_section_info(image, ^(const char *segmentName, const char *sectName, uint64_t vmAddr, uint64_t vmSize) {
            if (log) printf("\t\t%s,%s: (0x%llx - 0x%llx)\n", segmentName, sectName, vmAddr, vmAddr+vmSize);
        });

        dyld_image_local_nlist_content_4Symbolication(
            image, ^(const void *nlistStart, uint64_t nlistCount,
                     const char *stringTable) {
            if (nlistCount != 0) {
                XCTAssertNotNil((id)nlistStart);
                XCTAssertNotNil((id)stringTable);
            } else {
                XCTAssertNil((id)nlistStart);
                XCTAssertNil((id)stringTable);
            }
              printf("nlists: %p strings: %p\n", nlistStart, stringTable);
        });
    });
// TODO: Reenable once we switch to LSC and have Rosetta working
#if TARGET_OS_OSX
    dyld_shared_cache_for_each_subcache4Rosetta(cache, ^(const void* cacheBuffer, size_t cacheSize) {
        uuid_t uuid;
        uint64_t imagesCnt = 4096;
        const struct mach_header_64* images[4096] = {0};
        const void* baseAddress = nullptr;
        const void* aotExecutableBaseAddress = nullptr;
        uint64_t executableSize = 0;
        const void* aotWritableBaseAddress = nullptr;
        uint64_t writableSize = 0;
        XCTAssertTrue(dyld_shared_subcache_get_info4Rosetta(cacheBuffer, &uuid, &imagesCnt, images, &baseAddress, &aotExecutableBaseAddress,
                                                            &executableSize, &aotWritableBaseAddress, &writableSize));
        for (auto i = 0; i < imagesCnt; ++i) {
            XCTAssert(images[i]->magic == MH_MAGIC_64 || images[i]->magic == MH_MAGIC);
        }
    });
#endif
}
/*
- (void)testLocalIntrospection {
    kern_return_t kr = KERN_SUCCESS;
    dyld_process_t process = dyld_process_create_for_task(mach_task_self(), &kr);
    XCTAssertEqual(kr, KERN_SUCCESS);

    dyld_process_snapshot_t snapshot = dyld_process_snapshot_create_for_process(process, &kr);
    XCTAssertEqual(kr, KERN_SUCCESS);
    XCTAssertNotNil((id)snapshot);

    dyld_shared_cache_t cache = dyld_process_snapshot_get_shared_cache(snapshot);
    [self validateCache:cache log:NO];

    dyld_process_snapshot_dispose(snapshot);
    dyld_process_dispose(process);
}
*/
- (void)testLocalIntrospectionNullKernReturns {
    dyld_process_t process = dyld_process_create_for_task(mach_task_self(), nullptr);
    dyld_process_snapshot_t snapshot = dyld_process_snapshot_create_for_process(process, nullptr);
    dyld_shared_cache_t cache = dyld_process_snapshot_get_shared_cache(snapshot);
    [self validateCache:cache log:NO];
    dyld_process_snapshot_dispose(snapshot);
    dyld_process_dispose(process);
}

- (void)testFileSystemIntrospection {
    __block bool oneAndDone = false;
    dyld_for_each_installed_shared_cache(^(dyld_shared_cache_t cache) {
        if (oneAndDone) return;
        [self validateCache:cache log:NO];
        oneAndDone = true;
    });
};

#if TARGET_OS_OSX
-(void)testPinnedIntrospection {
    dyld_process_t process = dyld_process_create_for_task(mach_task_self(), nullptr);
    dyld_process_snapshot_t snapshot = dyld_process_snapshot_create_for_process(process, nullptr);
    dyld_shared_cache_t cache = dyld_process_snapshot_get_shared_cache(snapshot);
    XCTAssertTrue(dyld_shared_cache_pin_mapping(cache), "Pinning should always work on macOS");
    [self validateCache:cache log:NO];
    dyld4::Allocator allocator;
    __block dyld4::Vector<const void*> segmentAddresses(&allocator);

    dyld_shared_cache_for_each_image(cache, ^(dyld_image_t image) {
        dyld_image_for_each_segment_info(image, ^(const char *segmentName, uint64_t vmAddr, uint64_t vmSize, int perm) {
            XCTAssertTrue(dyld_image_content_for_segment(image, segmentName, ^(const void *content, uint64_t vmAddr, uint64_t vmSize) {
                segmentAddresses.emplace_back(content);
            }), "Content for segment should work for all segments");
        });
    });
    __block uint64_t i = 0;
    dyld_shared_cache_for_each_image(cache, ^(dyld_image_t image) {
        dyld_image_for_each_segment_info(image, ^(const char *segmentName, uint64_t vmAddr, uint64_t vmSize, int perm) {
            XCTAssertTrue(dyld_image_content_for_segment(image, segmentName, ^(const void *content, uint64_t vmAddr, uint64_t vmSize) {
                XCTAssertEqual(segmentAddresses[i++], content, "Returned addresses should be persistent with a pinned mapping");
            }), "Content for segment should work for all segments");
        });
    });

    dyld_shared_cache_unpin_mapping(cache);
    dyld_process_snapshot_dispose(snapshot);
    dyld_process_dispose(process);
}
#endif

#pragma clang diagnostic pop

@end
