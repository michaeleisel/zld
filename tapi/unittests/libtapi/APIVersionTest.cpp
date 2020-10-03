//===- /u/libtapi/APIVersionTest.cpp - libtapi API Version Interface Test -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "gtest/gtest.h"
#include <tapi/tapi.h>
#define DEBUG_TYPE "libtapi-test"

// Test the basic API Version query methods.
TEST(libtapi, APIVersion_getMajor) {
  EXPECT_EQ(tapi::APIVersion::getMajor(), TAPI_API_VERSION_MAJOR);
}

TEST(libtapi, APIVersion_getMinor) {
  EXPECT_EQ(tapi::APIVersion::getMinor(), TAPI_API_VERSION_MINOR);
}

TEST(libtapi, APIVersion_getPatch) {
  EXPECT_EQ(tapi::APIVersion::getPatch(), TAPI_API_VERSION_PATCH);
}

// Test the APIVersion comparison method.
TEST(libtapi, APIVersion_isAtLeast) {
  EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR));
  EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                          TAPI_API_VERSION_MINOR));
  EXPECT_TRUE(tapi::APIVersion::isAtLeast(
      TAPI_API_VERSION_MAJOR, TAPI_API_VERSION_MINOR, TAPI_API_VERSION_PATCH));

  EXPECT_FALSE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR + 1U));
  EXPECT_FALSE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                           TAPI_API_VERSION_MINOR + 1U));
  EXPECT_FALSE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                           TAPI_API_VERSION_MINOR,
                                           TAPI_API_VERSION_PATCH + 1U));

  if (TAPI_API_VERSION_MAJOR > 0) {
    EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR - 1U));
    EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR - 1U, ~0U));
    EXPECT_TRUE(
        tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR - 1U, ~0U, ~0U));
  }
  if (TAPI_API_VERSION_MINOR > 0) {
    EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                            TAPI_API_VERSION_MINOR - 1U));
    EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                            TAPI_API_VERSION_MINOR - 1U, ~0U));
  }
  if (TAPI_API_VERSION_PATCH > 0) {
    EXPECT_TRUE(tapi::APIVersion::isAtLeast(TAPI_API_VERSION_MAJOR,
                                            TAPI_API_VERSION_MINOR,
                                            TAPI_API_VERSION_PATCH - 1U));
  }
}

// Test the feature method.
TEST(libtapi, APIVersion_hasFeature) {
  // There are no features to test for yet.
}

// Test the ABI method.
TEST(libtapi, APIVersion_hasABI) {
  // There never has been an ABI v0.
  EXPECT_FALSE(tapi::APIVersion::hasABI(0));

  // Currently we only have ABI v1.
  EXPECT_TRUE(tapi::APIVersion::hasABI(1));

  EXPECT_FALSE(tapi::APIVersion::hasABI(2));
}
