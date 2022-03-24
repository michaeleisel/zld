# dyld4 design

The goal of dyld4 is to improve on dyld3 by keeping the same mach-o parsers, but do better in the non-customer case by supporting just-in-time loading that does not require a pre-built closures.

## Source code organization:
* __dyld/__ - the code that contributes to dyld
* __libdyld/__ - the code that contibutes to libdyld.dylib
* __cache-builder/__ - code for tools such as dyld_usage, dyld_info, dyld_shared_cache_builder, etc
* __other-tools/__ - code for tools such as dyld_usage, dyld_info, dyld_shared_cache_builder, etc
* __common/__ - common code shared between dyld and cache-builder 
* __include/__ - public headers
* __testing/__ - everything used to test dyld
* __doc/__ - man pages and other documentation
* __configs/__ - xcconfig files used by targets
* __build-scripts/__ - shell script phase scripts


## dyld
The model for dyld4 is that libdyld.dylib is thin and dyld contains all the runtime code.  

### Startup
The kernel starts processes by pushing all argc, argv, envp, and apple parameters onto the stack and jumping to dyld's entry point.  In dyld4 there are a few lines of assembly to align the stack and jump into C++ code with a pointer to the KernelArgs.  


### Global state
All dyld state is kept in dyld (not in libdyld) in two classes:

*  DyldProcessConfig keeps all fixed state info about the process (such are the security policy, the dyld cache, logging flags, platform, etc).  The object is constructed in dyld's `__DATA_CONST` segment before it is made read-only, so it is a const object.  
* The other state is kept in DyldRuntimeState, which includes all state which varies during the life of a process.  It includes a list Loader objects, the notification functions registered, and all locks.  

The DyldRuntimeState contains a pointer to the DyldProcessConfig object, so you can get to the `config` object from the `state` object.  To allow unit testing, the DyldRuntimeState is not a global variable. Instead, the DyldRuntimeState object is stack allocated in the start() function of dyld, and is passed as a parameter to any function that needs it. 


### Loader objects
Each loaded mach-o file is tracked by a `dyld4::Loader` object. 

In dyld2 we had a big ImageLoader graph.  In dyld3 we used an Array<> of two pointers. One was to the mach_header and the other was to the closure `Image`.  In dyld4, the loaded images list is an `Array<dyld4::Loader*>` in DyldRuntimeState.  

There is a `loadAddress()` method on the `dyld4::Loader*` to get the `mach_header*`.  For stuff in the dyld cache PrebuiltLoader's implementation of that method is fast because it is just an offset from the dyld cache header.  For PrebuiltLoaders not in the dyld cache, there is a parallel array of `mach_header` pointers.  On the other hand, JustInTimeLoader objects are malloc’ed and contain a direct pointer to its associated `mach_header` (just like ImageLoader does in dyld2).

### MachOAnalyzer
The dyld4::Loader objects do not directly parse/process mach-o files.  Instead, they are thin objects on top of the dyld3::MachOAnalyzer layer.  For now, we still have the MachOFile, MachOLoaded, and MachOAnalyzer split (from the dyld3 design).  We may want to merge MachOLoaded and MachOAnalyzer in the future, because that split does not make sense in a dyld4 world.


### No modes
In the dyld3 roll out, we were able to side step (postpone) issues in dyld3 by falling back to dyld2.  But that meant we had two implementations of everything in dyld and libdyld. It also made it confusing for our clients to know which dyld mode a process was running in.  

In dyld4 there is only one code base.  But, on an image by image basis, we instantiate either a PrebuiltLoader or a JustInTimeLoader   This means there is a nice continuum of performance.  A customer install gets PrebuiltLoader everywhere.  As more roots are installed on an Internal install, more JustInTimeLoaders are used which are not as fast. 


### Unified Prebuilt and JustInTime model
The current dyld3 model is problematic for two reasons: roots and versions.  We've seen lots of roots bugs, where we don’t invalidate some pre-computed info. But I also worry about closure versioning issues. Currently, we have pre-built closures in the dyld cache, then code to generate and process closures in dyld and libdyld.dylib. That means the three things are rev-locked, but no way to enforce that.  For instance, if we need to change the closure format for some security reason in macOS, we can install a new dyld and libdyld.dylib, but those need to be able to work against the old format closures that are in the dyld cache (until reboot).  We get away with this today by falling back to dyld2 mode. But we want to eliminate dyld2 mode.  

We designed dyld3 to be optimal for the common customer case where the OS is not changing and the apps are not changing.  With dyld3 we could (Ahead of Time) compute what dyld needed to do launch any process.  Computing a closure is expensive (time and space), but it is worthwhile when it is used over and over again.  But, we discovered over time, that inside Apple we are rarely in the common case.  The OS and apps are always changing (roots).  And with so much dlopen usage in the system, we often decide to use dyld3 mode, but then find during dlopen() that there are roots installed that invalidate all the pre-built closures, requiring them to be rebuilt which is expensive.  

The model for dyld4 is different. We have a new abstract base class `Loader`.  A Loader object is instantiated for each mach-o file loaded in a process. The Loader class has ten pseudo-virtual methods on it. There are two concrete subclasses of Loader:  `PrebuiltLoader` and `JustInTimeLoader`   The PrebuiltLoader and JustInTimeLoader class each have different implementations of the pseudo-virtual methods.  Example pseudo-virtual methods are: isValid(), loadAddress(), path(), applyFixups(), runInitializers(), dependent(), etc. 

The ten pseudo-virtual methods on these classes are not really C++ virtual because that requires a vtable pointer in the object and we want PrebuiltLoader objects to be mappable read-only from disk.  Instead, the start of each object contains a “kind" bit, and the base Loader implementation of those methods checks that field and jumps to either the PrebuiltLoader or JustInTimeLoader implementation.  

**One important constraint is that a PrebuiltLoader can only have other PrebuiltLoader as dependents.**  On the other hand, JustInTimeLoader can have either kind as dependents.  In the case where there are no roots, there can be a PrebuiltLoader for an application which is the top of a graph of PrebuiltLoader for everything needed to launch that app.  In the dyld shared cache there are pre-built PrebuiltLoader graphs for every program in the OS, and those graphs overlap by sharing nodes (e.g. the PrebuiltLoader objects for /usr/bin/true and /usr/bin/false both point to the same PrebuiltLoader for libSystem.B.dylib).

##### PrebuiltLoader and PrebuiltLoaderSet
A PrebuiltLoader is always read only.  It contains precomputed info about its mach-o file, including its path, validation info, its dependent dylibs, and an array of pre-computed bind targets. A 16-bit LoaderRef is used in PrebuiltLoader to references other Loaders.  Since PrebuiltLoader objects can only depend on other PrebuiltLoader objects, and there are most two PrebuiltLoaderSets in a process, a LoaderRef has bit to specify which PrebuiltLoaderSet and 15-bits to specify the index in that set.  Pre-computed bind targets are encoded as a <LoaderRef, offset>.

PrebuiltLoader objects are grouped into a `PrebuiltLoaderSet` which is a data structure that can be saved to disk and mmap()ed back in.  There are at most two PrebuiltLoaderSet objects available in any process.  One is in the dyld cache and contains a PrebuiltLoader for each dylib that is part of the dyld shared cache. The other is per-app PrebuiltLoaderSet.  

The per-app PrebuiltLoaderSet come from two locations: the dyld cache or from a file.  When the dyld cache is built, all OS main executables have a PrebuiltLoaderSet built and stored in the dyld cache.  There is a trie in the dyld cache that maps the program path to a PrebuiltLoaderSet.  

Dyld can be started in a mode where if the process uses JustInTimeLoader, after all images are loaded, but before any initializers are run, dyld "clones" the JustInTimeLoader objects to a PrebuiltLoader objects, then packs those into a PrebuiltLoaderSet and writes that to disk.  That saved PrebuiltLoaderSet is the equivalent of a dyld3 closure file.


##### JustInTimeLoader
Each JustInTimeLoader is a malloc()ed object that contains a pointer to its mach_header along with some flags and an array Loader* of its dependents.  It implements the ten pseudo-virtual methods by parsing its mach-o file as needed using MachOAnalyzer.   

At launch time, dyld looks for a pre-built PrebuiltLoader for the program. If found, its isValid() method is called which recursively calls isValid() on its dependent dylibs. If the program PrebuiltLoader is valid, it is used. If not, a new JustInTimeLoader is created and used instead.  The JustInTimeLoader then finds its dependents by parsing the mach-o.  For each dependent, the same trick is done of looking for an existing PrebuiltLoader to re-use.  When this is done, you have a graph with a JustInTimeLoader at the top and any subgraphs that are still valid use pre-built PrebuiltLoader and only the rooted dylib and images above it use JustInTimeLoader objects in the graph.

Because PrebuiltLoader and JustInTimeLoader objects are interchangeable, it gives a way to smoothly move between the two for testing coverage. For instance, if there is no dyld cache, there are no PrebuiltLoader objects - everything is JustInTimeLoader (like dyld2 mode).  On a customer iOS device, everything in the OS has a pre-built PrebuiltLoader  so all those launch at dyld3 speeds.  For development we can also have a boot arg or env var that controls when to force invalidate PrebuiltLoader which causes dyld to use a JustInTimeLoader   


## libdyld.dylib

libdyld.dylib is small.  Most all code is in dyld. The one exception is the \_dyld\_process\_info routines which don't use any current dyld state, but inspect the dyld state of another process.  It may make sense at some point to break out that code to a new dylib under libSystem.  

Given that libdyld.dylib is thin and just jumps into dyld, there are two interesting problems to solve: 1) what sort of jump table to use, 2) how the API functions in dyld get access to the DyldRuntimeState object.  The solution that solves both of these is to declare a new class `APIs` which is a subclass of `DyldRuntimeState` and has virtual methods for all APIs and SPIs in dyld.  Then at start up, dyld allocates an APIs object (instead of a  DyldRuntimeState object), and stuffs the address of it into a global variable (magic section) in libdyld.dylib.  In libdyld.dylib, there is glue for each API/SPI that just uses uses APIs pointer and calls the coresponding virtual method on it.  Additionaly, this design eases writing unit testing because now unit tests can call host dyld functions (e.g. dlopen("xx", 0)) or can call current code to test (e.g.  apis->dlopen("xx", 0)). 


#### Rationale for libdyld change
For dyld3, we went down the path of moving most code into libdyld.dylib.  The thought was that long term we would shrink dyld.  Eventually, the kernel could skip the load of dyld and instead start the process with the pc in libdyld.dylib (in the dyld cache), and the actual dyld would only be needed in bootstrapping cases where the dyld cache is out of date (or a dyld root installed).  


But, the current dyld/libdyld split is problematic:
1) The interface to hand over control from dyld to libdyld in dyld3 mode is a complicated multi-step process
2) It is unclear if we will ever be able to unload dyld once control is handed over because of various global state in dyld
3) Closure versioning will be very complicated to solve.  The worst case scenario is that roots are installed such that dyld, libdyld.dylib, and the cache builder version used to create the active dyld cache are all different versions. There is no way to get the closures to interoperate because they may have a different format (being different versions).
4) If we could get to the point where the kernel starts a procces in libdyld.dylib in the dyld cache, it is unclear how libSystem can be initialized.  It can only be initialized after all images in libSystem are loaded and bound together.  But, how can libdyld.dylib load libSystem roots if it can’t use parts of libsystem to do that work?

The dyld4 solution is:
A) Go back to putting all code in dyld
B) Make libdyld.dylib be a thin shim where APIs jump back into dyld using a table of function pointers.
C) As an optimization, have the cache builder put a copy of dyld into the dyld cache.  Then change the kernel (when alt-dyld is not in use) to skip the dyld load and instead set the initial pc to be in dyld in the dyld cache.

This solves the above issues:
1) There is no hand off anymore. 
2) Dyld will never be unloaded, but we get many of those wins by putting dyld into the shared cache
3) There is no longer a need for closure versioning.  libdyld.dylib no longer contains closure reading/writing code, so it cannot be out of sync with dyld.  And for dyld4 if there are any roots, the PrebuiltLoader objects in the dyld cache will be invalid, so dyld will run the process using JustInTimeLoader objects
4) Because dyld still builds with a static libc.a, the libSystem dylib code is not initialized until dyld is done loading all imaage.


### Testing
The testing/ dir has two major sub-dirs:  test-cases and unit-tests.  The first generates the BATS tests from .dest directories, just like in dyld3.  The second directory is new and uses XCTest.  

#### unit-tests
There is one \*Tests.mm file for each chunk of dyld internals to test.   Currently: DyldProcessConfigTests.mm, APITests.mm, and MachOFileTests.mm. The files are .mm because XCTest uses ObjC and dyld internals are written in C++.  

A typical unit tests follows the Arrange, Act, Assert pattern.  That is, the Arrange part sets up the needed objects, the Act part calls methods on the objects to excerise the functionality being tested, and lastly the Assert part uses XCTAssert() macros to verify the methods did what was expected.

Note, XCTest runs all \*test methods in parallel. That means there can be no global variables in dyld, as the tests will interfere with each others uses of the globals.  That is why all dyld "global variables" must be placed as fields in either ProcessConfig (if fixed for the life of the process) or in RutimeState.  

For API testing, unit tests must call methods on the the APIs class (not the global C function that is the normal dyld API).  This is because calling the global function will actually call that function in the host dyld.  


#### Delegates

To support testability and using dyld code in the cache builder, none of the dyld code makes direct OS calls or directly access kernel args.  Instead the calls go through delegate objects.  This allows different delegate objects to be used during unit tests or during shared cache building.

##### SyscallDelgate
The SyscallDelegate handles all OS calls (such an opening or mapping files). There are low level (i.e. posix level) methods like open, close, mmap.  And there are higher level methods like withReadOnlyMappedFile() that make it easy to swap in different implements such as needed MRM where all files are already mapped in memory.

Besides syscalls, the SyscallDelegate also provides access to the commpage and boot-args.  Basically, information provide by the OS.

The current implementation just has one SyscallDelegate class which has #if directives to build differently in different targets.  Currently there is no need for dynamic choice of which SyscallDelegate to use, so #if works fine.

##### KernelArgs
The other delegate is KernelArgs which a pointer to the info the kernel passes on the stack to dyld (e.g. argc, argv, envp, etc).  This is less delegatey than the others because it is an open data structure.  But the KernelArgs class provides an easy wrapper for unit tests to construct kernel arguments for testing ProcessConfig.


#### MockO

There is much functionality in dyld that processes mach-o files.  In theory, you could delegate that out such that the logic on top of the raw mach-o parses could be tested, but that still leaves the mach-o parsing to be tested.  So, instead, we have a helper class that can generate in-memory mach-o files on the fly.  These are "mocks" in unit testing terminology.  The helper class that generates the mach-o files is call MockO.  To use, you simple contstruct a MockO object passing in the filetype and architecure, then use methods to add the load commands and content you need to test.  

The MockO class is still work in progress.  Currently it only produces valid mach_header and load commands, but that is enough to write unit tests to test ProcessConfig which needs to inspect the main executable.


