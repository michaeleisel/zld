/*
* Copyright (c) 2019 Apple Inc. All rights reserved.
*
* @APPLE_LICENSE_HEADER_START@
*
* "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
* Reserved.  This file contains Original Code and/or Modifications of
* Original Code as defined in and that are subject to the Apple Public
* Source License Version 1.0 (the 'License').  You may not use this file
* except in compliance with the License.  Please obtain a copy of the
* License at http://www.apple.com/publicsource and read it before using
* this file.
*
* The Original Code and all software distributed under the License are
* distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
* EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
* License for the specific language governing rights and limitations
* under the License."
*
* @APPLE_LICENSE_HEADER_END@
*/

// For now just for compact info, but in the long run we can use this to centralize convenience macros and configuration options

#ifndef DYLD_DEFINES_H
#define DYLD_DEFINES_H

#include <TargetConditionals.h>

#define VIS_HIDDEN __attribute__((visibility("hidden")))

#define TRIVIAL_ABI [[clang::trivial_abi]]

#define SUPPORT_ROSETTA (TARGET_OS_OSX && __x86_64__)

#if TARGET_OS_OSX && defined(__x86_64__)
#define SUPPPORT_PRE_LC_MAIN (1)
#else
#define SUPPPORT_PRE_LC_MAIN (0)
#endif

#ifndef BUILD_FOR_TESTING
#define BUILD_FOR_TESTING 0
#endif

#ifndef BUILDING_UNIT_TESTS
#define BUILDING_UNIT_TESTS 0
#endif

#ifndef BUILDING_DYLD
#define BUILDING_DYLD 0
#endif

#ifndef BUILDING_LIBDYLD
#define BUILDING_LIBDYLD 0
#endif

#ifndef BUILDING_LIBDYLD_INTROSPECTION_STATIC
#define BUILDING_LIBDYLD_INTROSPECTION_STATIC 0
#endif

#ifndef BUILDING_CACHE_BUILDER
#define BUILDING_CACHE_BUILDER 0
#endif

#if !TARGET_OS_DRIVERKIT && (BUILDING_LIBDYLD || BUILDING_DYLD)
  #include <CrashReporterClient.h>
#else
  #define CRSetCrashLogMessage(x)
  #define CRSetCrashLogMessage2(x)
#endif

#endif /* DYLD_DEFINES_H */
