// Copyright Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

// The keys amdcuid_set_hash_key() refuses, and who may set or read the key.

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstring>
#include <initializer_list>

#include "include/amd_cuid.h"
#include "src/hmac.h"

TEST(cuidtstUnprivileged, PublicAndTrivialKeysAreRejected) {
  uint8_t key[key_length];
  for (const uint8_t fill : std::initializer_list<uint8_t>{0x00, 0x41, 0xff}) {
    std::memset(key, fill, sizeof(key));
    EXPECT_TRUE(CuidUtilities::is_rejected_key(key)) << int(fill);
  }
  for (const char* constant : {"AMD-CUID-DEFAULT-SEED-v1", "AMD-CUID-TEMP-KEY-v1"}) {
    std::memset(key, 0, sizeof(key));
    std::memcpy(key, constant, std::strlen(constant));
    EXPECT_TRUE(CuidUtilities::is_rejected_key(key)) << constant;
  }
  for (size_t i = 0; i < key_length; ++i) key[i] = static_cast<uint8_t>(0xA5 ^ i);
  EXPECT_TRUE(CuidUtilities::is_rejected_key(key));
  for (size_t i = 0; i < key_length; ++i) key[i] = static_cast<uint8_t>(i);
  EXPECT_TRUE(CuidUtilities::is_rejected_key(key));

  key[0] = 0x80;
  EXPECT_FALSE(CuidUtilities::is_rejected_key(key));
  std::memset(key, 0, sizeof(key));
  std::memcpy(key, "AMD-CUID-TEMP-KEY-v1", 20);
  key[31] = 1;
  EXPECT_FALSE(CuidUtilities::is_rejected_key(key));
}

// Root is never exercised here: a key this suite passes would re-key amdgpu.
TEST(cuidtstUnprivileged, SetHashKeyNeedsRoot) {
  if (geteuid() == 0) GTEST_SKIP() << "only an ordinary user can call it safely";
  uint8_t key[key_length];
  for (size_t i = 0; i < key_length; ++i) key[i] = static_cast<uint8_t>(0x40 + 3 * i);
  EXPECT_EQ(amdcuid_set_hash_key(key), AMDCUID_STATUS_PERMISSION_DENIED);
}

TEST(cuidtstUnprivileged, KeyInfoNeedsRoot) {
  if (geteuid() == 0) GTEST_SKIP() << "root may read the key";
  amdcuid_key_info_t info;
  std::memset(&info, 0xff, sizeof(info));
  EXPECT_EQ(amdcuid_get_key_info(&info), AMDCUID_STATUS_PERMISSION_DENIED);
  const amdcuid_key_info_t cleared{};
  EXPECT_EQ(std::memcmp(&info, &cleared, sizeof(info)), 0);
  EXPECT_EQ(amdcuid_get_key_info(nullptr), AMDCUID_STATUS_INVALID_ARGUMENT);
}
