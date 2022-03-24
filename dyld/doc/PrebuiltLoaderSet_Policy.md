# PrebuiltLoaderSet Policy

dyld4 must have a Loader object for each loaded mach-o file. There are two kinds of Loader objects: PrebuiltLoader and  JustInTimeLoader. PrebuiltLoader objects are smaller, faster, and read-only.  JustInTimeLoader objects are malloc()ed objects which parse the mach-o file on the fly.  

PrebuiltLoader objects are grouped into PrebuiltLoaderSet objects which are always in read-only memory.  For OS programs, the dyld cache builder creates PrebuiltLoaderSet objects in the dyld cache.  For all other programs, PrebuiltLoaderSet objects are built as needed and saved to disk. In dyld3 terminology, a saved PrebuiltLoaderSet would be called a closure file. 

The document describes the policy for when PrebuiltLoaderSet files are used in dyld4.



## Background: dyld3 policy
Recent OS releases, contained both dyld2 and dyld3, and we had a policy on when to use dyld3:

* iOS/tvOS/watchOS: when device booted using customer dyld cache, all 3rd party apps and all programs built into the OS use dyld3. Other non-containerized programs run in dyld2
* iOS/tvOS/watchOS: when device booted using development dyld cache, all programs use dyld2
* macOS: programs built into the OS used dyld3, unless a "root" was detected
* macOS: All Almond (iPad apps) run in dyld3 mode
* macOS: All other programs (including all 3rd party apps) run in dyld2 mode


The above policy came about for a number of pragmatic reasons:

* Internal installs (which use development dyld cache) often have "roots" installed, and dyld3 has bugs invalidating closures.
* dyld3 is stricter than dyld2 about mach-o file layout.  Since all 3rd party apps for iOS go through the app store, we were able to qualify dyld3 against all apps.  Whereas macOS has no clearing house for apps, many of which are built by 3rd party tools.
* On iOS/tvOS/watchOS, all 3rd party apps are "containerized" which means they have an app specific home directory and are sandboxed to only be able to write files there.

The end result of the above is that the only place we needed to build and save closure files to disk were for 3rd party apps on iOS/tvOS/watchOS when running with a customer dyld cache.  



## dyld4 constraints

With dyld4 there is no choice of falling back to dyld2.  There is only dyld4.  The only option is using JustInTimeLoaders or PrebuiltLoaders.  

For dyld4 we want the ability to build and save PrebuiltLoaderSets for all programs.  This is because on macOS very few apps are containerized, and on iOS there are many built-in applications that are not containerized.  For non-containerized apps, we will need to save the PrebuiltLoaderSet file to some unique name within ~/Library/Caches/com.apple.dyld/ so that different apps don't conflict with each other.

There are a couple of cases where dyld4 will never try to build a PrebuiltLoaderSet:

* no dyld cache (the PrebuiltLoaderSet would be huge)
* main program is linked with a dylib that has interposing (complicates PrebuiltLoaderSet and only used during development)
* main program launched with `DYLD_INSERT_LIBRARIES`, `DYLD_*_PATH`, or `DYLD_IMAGE_SUFFIX` (complicates PrebuiltLoaderSet and only used during development)

An apple engineer working on a program which is normally part of the OS would like to be able to get PrebuiltLoader performance.  But there is a PrebuildLoaderSet in the dyld cache.  We would need a way to override that PrebuildLoaderSet, but not introduce a security hole where OS program could be hijacked by installing an alternate PrebuildLoaderSet file.

For testing, we'd like the ability to force dyld4 to use or not use PrebuiltLoaderSets via `DYLD_USE_CLOSURES`.

We also continue to have the security requirement that closure files are not used after a reboot.  That is to deny attacks a way to persist.


## dyld4 policy

dyld4 will build and save PrebuiltLoaderSet except cases when it cannot, which are:

* no dyld cache
* the dyld cache version of PrebuiltLoaderSet does not match dyld
* process launched with`DYLD_INSERT_LIBRARIES`, `DYLD_*_PATH`, or `DYLD_IMAGE_SUFFIX` 
* There is already a valid PrebuiltLoaderSet in the dyld cache
* Interposing is used
* `DYLD_USE_CLOSURES` is used to override the default policy

This differs from GoldenGate/Azul in that PrebuiltLoaderSets (closure files) are now saved/read:

* with development dyld cache
* with non-containerized apps
* on macOS
* with rooted OS programs on Internal installs

It is possible to override the default policy using `DYLD_USE_CLOSURES`.  That env var currently has the following meaning:

* `DYLD_USE_CLOSURES=0` means use dyld2
* `DYLD_USE_CLOSURES=1` means use dyld3
* `DYLD_USE_CLOSURES=2` means use dyld3s (smaller closure)

The dyld BATS test cases current run each test with three values of `DYLD_USE_CLOSURES` to ensure the test works when dyld is runnning in each of those modes.

For dyld4, we've remapped that env var to have the folloing meanings:

* `DYLD_USE_CLOSURES=0` means use JITLoader for main executable
* `DYLD_USE_CLOSURES=1` means use JITLoader for main executable and then build a PrebuiltLoaderSet from JITLoaders and save to disk
* `DYLD_USE_CLOSURES=2` means look for PrebuiltLoaderSet and fail if one is not already built


## Implementation details

A full internal install of Xcode may have five or so clang instances, each in a different toolchain.  We want to write a PrebuiltLoaderSet so future clang startups are faster, but clang is not containerized, and we don't want each version of clang stomping on each other's PrebuiltLoaderSet files.  Therefore, the path to the PrebuiltLoaderSet file is a combination of the cdhash for the binary (passed to dyld from the kernel) and a hash of the path to the main executable.  


It is easy to check if there is a dyld cache or if there are `DYLD_*` env vars and disable checking for or building a PrebuiltLoaderSet.  But interposing is harder.  The existing of interposing is not known until all dylibs are loaded.  That means `DYLD_USE_CLOSURES=2` cannot be an error early in ProcessConfig. We have to wait for the JustInTimeLoaders are built and see that a PrebuiltLoader could have been built.  


