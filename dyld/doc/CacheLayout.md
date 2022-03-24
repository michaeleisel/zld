# Shared Cache Layout

## Overview

This document describes the shared cache layout both on-disk and in memory.

Some terminology, to help understand what follows:

* Shared cache - The file(s) we are describing in this document
* Shared region - A shared cache which has been loaded at run time.  Each process can be attached to a single shared region in the kernel
* VM - Virtual Memory, which is typically used when describing the layout of the cache at run time
* Mapping - a contiguous region of memory in the shared cache, where the region of memory all has the same permissions
* Page tables - There are different tiers, but we tend to only care about the highest level.  This is the minimum granularity on which permissions should be distinct

## Single Cache File Layout

Generating a single cache file is the simplest possible layout, and also covers most of other interesting aspects/constraints when discussing shared caches.  All caches prior to the 2021 releases used a single cache file layout.

The table below represents a typical x86_64 shared cache.

Mapping Name | Start Address  | End Address    | Permissions  |
:-----------:|:--------------:|:--------------:|:------------:|
TEXT         | 0x7FFF20000000 | 0x7FFF7FA64000 | read/execute |
DATA         | 0x7FFF80000000 | 0x7FFF8E214000 | read/write   |
LINKEDIT     | 0x7FFFC0000000 | 0x7FFFE2D9C000 | read-only    |

### Building a shared cache

The above cache file has 3 mappings - TEXT, DATA, LINKEDIT.  There are many aspects of building a cache, but to keep things simple, all we care about is copying the segments in each dylib in to the corresponding mapping.  That part of the cache building process is fairly simple.  We iterate over each source dylib, and copy its segments in to the appropriate mapping above.

For example, a typical dylib might contain the following segments:

Segment Name | Permissions  |
:-----------:|:------------:|
TEXT         | read/execute |
DATA         | read/write   |
DATA_CONST   | read/write   |
DATA_DIRTY   | read/write   |
LINKEDIT     | read-only    |

We have only a single read/execute segment (TEXT) which we copy in to the read/execute mapping (also called TEXT).  For read/write, we have 3 segments (DATA, DATA\_CONST, DATA\_DIRTY), which are all copied in to the single read/write mapping (called DATA).  Finally, we copy all LINKEDIT from the source dylibs in to the LINKEDIT mapping, as these are all read-only.

### Aligning the shared cache mappings

You may have noticed that the above cache layout has large gaps between the mappings.  This is due to page tables.

In the kernel, there are wired data structures (page tables) which map each VM page in each process to its backing RAM page or file. Normally, the kernel maintains separate wired page tables for each process. But as an optimization, if multiple processes happen to have the exact same page tables for a range, the kernel will just have one copy that is shared by all the processes that can. On x86_64 that sharable range is 1GB. On arm64 the range is 32MB range. To save lots of wired memory in the kernel, we arrange the read-only parts of the dyld shared cache to enable the kernel to share page tables. That means on x86\_64 we need the r/w regions to be in separate 1GB address ranges from the read-only parts of the cache, and ASLR just slides with those 1GB ranges. For arm64, we get the same result by having the cache builder introduce 32MB of address space between r/w and r/o regions. Then, no matter the ASLR slide, the r/o parts can never be in the same 32MB page table region as a r/w part, and thus the kernel can share the page tables for the r/o parts of the dyld cache.

## Large Cache Layout

The perceptive reader may have noticed that x86_64 has 1GB requirements on the highest possible page-table tier, but the earlier layout doesn't align DATA to 1GB.  Ideally TEXT would be 2GB in size, then DATA 1GB, and finally LINKEDIT 1GB.  But actually we have 1.5GB of TEXT, 1GB of DATA, and 1GB of LINKEDIT.

This is due to other constraints in the platform.  In this case, the instruction set itself.  A load instruction in x86_64 has only +-2GB of reach from the load to the data it is loading.  In our case, the load instruction (in TEXT) must be within 2GB of the value (in DATA).  This means that we can't make TEXT 2GB in size, as then all of DATA is out of reach.  We could make TEXT 1GB in size, but that would evict significant numbers of dylibs from the cache.

The solution is to trade some page-tables for more dylibs being in the cache.  We don't achieve a 2GB page-table for TEXT, just 1.5GB, but we manage to include many more dylibs in the cache.

We actually only have about 200MB of DATA, so it should be possible to make TEXT about 1.75GB, and DATA 250MB.  However, that breaks Rosetta, which has its own constraints on the cache layout.

### Splitting the cache into multiple files

Increasing TEXT to 1.5GB allowed for many more dylibs in the cache, but we eventually hit this limit, and evicted dylibs to disk.  The solution is to split the cache into multiple files.

On x86_64, each file is still subject to the 1GB alignment constraint on page-tables, but when we are going to exceed those constraints, we can just start a new file.  The kernel was changed to give us 32GB of space for the shared region, so we have plenty of room to grow the number of cache files as we need more space.

Cache file # | Mapping Name | Start Address  | End Address    | Permissions  |
------------:| :-----------:|:--------------:|:--------------:|:------------:|
0            | TEXT         | 0x7FF800000000 | 0x7FF81FA10000 | read/execute |
0            | DATA_CONST   | 0x7FF840000000 | 0x7FF841D9C000 | read/write   |
0            | DATA         | 0x7FF841D9C000 | 0x7FF8437BC000 | read/write   |
0            | LINKEDIT     | 0x7FF880000000 | 0x7FF88BF84000 | read-only    |
1            | TEXT         | 0x7FF900000000 | 0x7FF92041C000 | read/execute |
1            | DATA_CONST   | 0x7FF940000000 | 0x7FF941EA0000 | read/write   |
1            | DATA         | 0x7FF941EA0000 | 0x7FF944814000 | read/write   |
1            | LINKEDIT     | 0x7FF980000000 | 0x7FF98C9C4000 | read-only    |
2            | TEXT         | 0x7FFA00000000 | 0x7FFA1FEF4000 | read/execute |
2            | DATA_CONST   | 0x7FFA40000000 | 0x7FFA42A38000 | read/write   |
2            | DATA         | 0x7FFA42A38000 | 0x7FFA44C90000 | read/write   |
2            | LINKEDIT     | 0x7FFA80000000 | 0x7FFA874A8000 | read-only    |
3            | TEXT         | 0x7FFB00000000 | 0x7FFB1B940000 | read/execute |
3            | DATA_CONST   | 0x7FFB40000000 | 0x7FFB42EC4000 | read/write   |
3            | DATA         | 0x7FFB42EC4000 | 0x7FFB4421C000 | read/write   |
3            | LINKEDIT     | 0x7FFB80000000 | 0x7FFB8A284000 | read-only    |

The above is about 2GB of TEXT, split over 4 files.  It additionally has DATA\_CONST mappings, which are very similar to DATA.  For now DATA\_CONST is mapped adjacent to DATA, and not aligned to 1GB.  Note DATA\_CONST was an independent feature from new cache layout, but is listed here as all new cache layouts include it.

This layout has a number of benefits over the previous layout.

Now all mappings with different permissions are on their own 1GB regions, so we have the best possible use of page-tables.  Additionally, the cache has more slide available.  At run time we choose a slide value, and move the cache to that location.  The maximum slide in the old layout was 1.5GB minus the TEXT mapping size.  But as the TEXT mapping was almost exactly 1.5GB, the slide was effectively zero.  On this new layout, we choose to make a new cache file for each 512MB of TEXT, so the slide is about 512MB too (so that we don't cross 1GB boundaries).

### arm64e and Large Caches

arm64e devices also use the Large Cache layout.  Similar to the x86\_64 issue with a +-2GB reach on the load instruction, we have a limit on arm64(e) too.  In this case, the limit is not the load instruction (arm64 has +-4GB reach on loads), but is instead due to metadata.  In TEXT, we have metadata such as unwind information, objc relative method lists, and Swift constants, which use int32_t's to point to values in DATA.  The limit on an int32\_t is +-2GB, and so the TEXT and DATA mappings have to be within 2GB of each other.  On our devices, we exceeded this limit.

The solution to exceeding this limit is again to use Large Shared Caches.  As you can see the previous x86_64 layout, each cache file in the large layout has its own TEXT and DATA, so we can keep them close to each other.

An example arm64e Large Cache layout is as follows.

Cache file # | Mapping Name | Start Address  | End Address    | Permissions  |
------------:| :-----------:|:--------------:|:--------------:|:------------:|
0            | TEXT         | 0x180000000    | 0x1CC708000    | read/execute |
0            | DATA_CONST   | 0x1CE708000    | 0x1D115C000    | read/write   |
0            | DATA         | 0x1D315C000    | 0x1D5EA4000    | read/write   |
0            | AUTH         | 0x1D7EA4000    | 0x1D9548000    | read/write   |
0            | AUTH_CONST   | 0x1DB548000    | 0x1DE780000    | read/write   |
0            | LINKEDIT     | 0x1E2780000    | 0x1E27AC000    | read-only    |
1            | TEXT         | 0x1E27AC000    | 0x203BB0000    | read/execute |
1            | DATA_CONST   | 0x205BB0000    | 0x20848C000    | read/write   |
1            | DATA         | 0x20A48C000    | 0x20B3DC000    | read/write   |
1            | AUTH         | 0x20D3DC000    | 0x20DFA4000    | read/write   |
1            | AUTH_CONST   | 0x20FFA4000    | 0x2110B0000    | read/write   |
1            | LINKEDIT     | 0x2150B0000    | 0x23B468000    | read-only    |

As this is an arm64e cache, it has additional mappings for authenticated data.  These contain pointers which are signed with Pointer Authentication.  AUTH corresponds to DATA with authenticated pointers, and AUTH\_CONST is anything which would normally be placed in DATA\_CONST, but has authenticated pointers. In order to minimize address space and adjacent page table usage segments with globally shared page tables are grouped with no padding (LINKEDIT, TEXT, DATA\_CONST), as are all segments with per process page tables (DATA, AUTH, AUTH\_CONST). Because there is no padding between the LINKEDIT of one subcache and the TEXT of the next subcache this results in two sections of padding per subcache, the minimum about of padding possible while still allowing arbitrary ASLR slides.

Notice that the above differs from x86_64 in that has a minimal LINKEDIT in the first cache file.  This is an optimization.  LINKEDIT isn't subject to 2GB limits, but instead 4GB.  As the maximum size of the arm64(e) shared region is 4GB, we know LINKEDIT will be in reach.  Having a single large LINKEDIT allows optimizations such as deduplicating symbol strings to be more effective.  The minimal LINKEDIT in the first cache file contains only the information to slide the cache file.  It does not contain other LINKEDIT, such as symbol information.

### The arm64 branch predictor issue

The above Large Cache layout for arm64e does have a downside; it causes TEXT to exceed 2GB of address space range.  That is, the end of the second TEXT is over 2GB from the start of the first TEXT.  This is an issue for the branch predictor which can only efficiently handle 2GB total.  The large cache layout makes this worse, as to avoid the 2GB metadata issue, we need to move DATA closer to TEXT.  But by being closer, some DATA inevitably ends up between the first TEXT and the second TEXT, and consumes address space which would otherwise be useful for the branch predictor.  While large caches made this worse, crossing this limit was inevitable given growth in the OS.

## Split Cache Layout

The final layout we support is called Split Cache.  This is equivalent to splitting the cache file into pieces, but not changing the layout of the mappings inside.  The primary need for this is on watchOS where devices have limited amounts of RAM (1GB) and we couldn't fit the cache in memory to perform OTA patching.

The split cache layout solves this by generating a new cache file every 128MB of TEXT.  Additionally, DATA and LINKEDIT are in their own cache files.  An example armv7k split cache layout is as follows:

Cache file # | Mapping Name | Start Address  | End Address    | Permissions  |
------------:| :-----------:|:--------------:|:--------------:|:------------:|
0            | TEXT         | 0x40000000     | 0x47788000     | read/execute |
1            | TEXT         | 0x47788000     | 0x4FB50000     | read/execute |
2            | TEXT         | 0x4FB50000     | 0x57408000     | read/execute |
3            | TEXT         | 0x57408000     | 0x5F0D8000     | read/execute |
4            | TEXT         | 0x5F0D8000     | 0x66CAC000     | read/execute |
5            | TEXT         | 0x66CAC000     | 0x6E89C000     | read/execute |
6            | TEXT         | 0x6E89C000     | 0x70698000     | read/execute |
7            | TEXT         | 0x70698000     | 0x706D0000     | read/execute |
7            | DATA         | 0x70AD0000     | 0x72C40000     | read/write   |
7            | DATA_CONST   | 0x72C40000     | 0x756D8000     | read/write   |
7            | LINKEDIT     | 0x75AD8000     | 0x75B00000     | read-only    |
8            | LINKEDIT     | 0x75B00000     | 0x75B38000     | read-only    |
8            | LINKEDIT     | 0x75B38000     | 0x7C64C000     | read-only    |

The TEXT and LINKEDIT in files 7 and 8 are worth noting.  The TEXT in file 7 isn't execuable code.  Instead it is just a cache header, but as the previous files were all TEXT, we start with TEXT here for consistency.  This gives us one contiguous range of TEXT starting from file 0 and ending here on file 7.

In a similar way to the arm64e Large Cache layout, file 7 contains DATA and a minimal LINKEDIT.  This LINKEDIT is just enough to know how to slide the DATA, but doesn't contain typical LINKEDIT metadata such as strings or symbols.

File 8 also starts with a cache header, but the main payload of this file is LINKEDIT.  Given that file 7 ended with LINKEDIT, and file 8 contains only LINKEDIT, we change the file 8 cache header from TEXT to LINKEDIT.  This is inconsistent with any other caches, where the cache header is always in TEXT, but has the advantage that we get a contiguous range of read-only mappings, starting from the end of file 7, and ending in file 8.

### arm64 and Split Caches

Currently arm64 devices also use the Split Cache layout.  This will likely change to the Large Cache layout in future, when the total TEXT + DATA size exceeds the 2GB reach of the metadata, as described earlier.  The arm64 Split Cache layout is equivalent to the above armv7k one, just with different ranges of addresses.

## Symbols cache file

On embedded devices, local symbols are transferred to an unmapped area of the cache.  They are present in the file on disk, for analysis tools to use, but they are not present in memory at run time.

In the old cache layout, these unmapped local symbols were contained at the end of the single cache file.  In these new layouts (both large and split), these unmapped symbols are now in their own file.  This has a couple of advantages.  Firstly, these symbols files are identical on customer vs developer devices, and so we can share the (400MB) symbols file between both cache configurations to save disk space.   Secondly, in the old cache layout, these unmapped symbols were covered by the code signature of the cache file.  This code signature is copied by the kernel in to wired memory which is never released.  By moving the symbols to their own file, they are no longer in the cache code signature range, saving 1-3MB of wired memory as we don't need that piece of the code signature at run time.

## Summary of cache layouts

This table represents the caches we build today, and which cache layout they use.  Note that where the platform is "all", that means all platforms not covered by a more specific row.

Cache architecture | Platform  | Cache kind | Cache kind reason
-------------------|:---------:| :---------:|------------------
x86_64             | macOS     | Large      | Instruction set limitations
x86_64h            | macOS     | Large      | Instruction set limitations
arm64              | all       | Split      | OTA updates/compression of cache files
arm64e             | all       | Large      | 32-bit signed integers in TEXT->DATA metadata
armv7k             | watchOS   | Split      | OTA updates/compression of cache files
arm64_32           | watchOS   | Split      | OTA updates/compression of cache files
x86_64             | simulator | Regular    | Back deployment to old macOS releases
x86_64h            | simulator | Regular    | Back deployment to old macOS releases
arm64              | simulator | Regular    | Back deployment to old macOS releases
