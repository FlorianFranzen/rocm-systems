// Copyright (c) 2026 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

/// @file transcendental_test.cpp
/// @brief Phase C unit tests for shared transcendental functions.

#include "rocjitsu/isa/arch/amdgpu/shared/transcendental.h"
#include "util/amdgpu_exp.h"
#include "util/amdgpu_log.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace {

using namespace rocjitsu::amdgpu::transcendental;

// ---------------------------------------------------------------------------
// Special-case tests (±0, ±Inf, NaN, denormals)
// ---------------------------------------------------------------------------

TEST(TranscendentalTest, RcpF32SpecialCases) {
  EXPECT_EQ(rcp_f32(0.0f), std::numeric_limits<float>::infinity());
  EXPECT_EQ(rcp_f32(-0.0f), -std::numeric_limits<float>::infinity());
  EXPECT_EQ(rcp_f32(std::numeric_limits<float>::infinity()), 0.0f);
  EXPECT_EQ(rcp_f32(-std::numeric_limits<float>::infinity()), -0.0f);
  EXPECT_TRUE(std::isnan(rcp_f32(std::numeric_limits<float>::quiet_NaN())));
  EXPECT_FLOAT_EQ(rcp_f32(2.0f), 0.5f);
}

TEST(TranscendentalTest, RsqF32SpecialCases) {
  EXPECT_EQ(rsq_f32(0.0f), std::numeric_limits<float>::infinity());
  EXPECT_TRUE(std::isnan(rsq_f32(-1.0f)));
  EXPECT_EQ(rsq_f32(std::numeric_limits<float>::infinity()), 0.0f);
  EXPECT_TRUE(std::isnan(rsq_f32(std::numeric_limits<float>::quiet_NaN())));
  EXPECT_FLOAT_EQ(rsq_f32(4.0f), 0.5f);
}

TEST(TranscendentalTest, SqrtF32SpecialCases) {
  EXPECT_TRUE(std::isnan(sqrt_f32(-1.0f)));
  EXPECT_EQ(sqrt_f32(0.0f), 0.0f);
  EXPECT_EQ(sqrt_f32(std::numeric_limits<float>::infinity()),
            std::numeric_limits<float>::infinity());
  EXPECT_FLOAT_EQ(sqrt_f32(4.0f), 2.0f);
}

TEST(TranscendentalTest, LogF32SpecialCases) {
  EXPECT_EQ(log_f32(0.0f), -std::numeric_limits<float>::infinity());
  EXPECT_TRUE(std::isnan(log_f32(-1.0f)));
  EXPECT_EQ(log_f32(std::numeric_limits<float>::infinity()),
            std::numeric_limits<float>::infinity());
  EXPECT_FLOAT_EQ(log_f32(1.0f), 0.0f);
  EXPECT_FLOAT_EQ(log_f32(4.0f), 2.0f);
}

TEST(TranscendentalTest, ExpF32SpecialCases) {
  EXPECT_EQ(exp_f32(-std::numeric_limits<float>::infinity()), 0.0f);
  EXPECT_EQ(exp_f32(std::numeric_limits<float>::infinity()),
            std::numeric_limits<float>::infinity());
  EXPECT_FLOAT_EQ(exp_f32(0.0f), 1.0f);
  EXPECT_FLOAT_EQ(exp_f32(1.0f), 2.0f);
}

TEST(ExpLogScalarModelTest, ExpMatchesPhysicalRdna3AndRdna4) {
  // Raw instruction results captured independently on gfx1100 and gfx1201.
  // Include staged rounding, negative reduction, and range boundaries.
  const uint32_t captured[][2] = {
      {0x337fffffu, 0x3f800000u}, {0x33800000u, 0x3f800000u}, {0x33800001u, 0x3f800000u},
      {0xb37fffffu, 0x3f800000u}, {0xb3800000u, 0x3f7fffffu}, {0xb3800001u, 0x3f7fffffu},
      {0x42ffffffu, 0x7f7fffa7u}, {0x43000000u, 0x7f800000u}, {0xc2fc0000u, 0x00800000u},
      {0xc2fc0001u, 0x00000000u}, {0x3f0567ecu, 0x3fb7b03du}, {0xc114ed44u, 0x3acecc1eu},
      {0x35500000u, 0x3f800004u}, {0x3e000090u, 0x3f8b95d0u}, {0x3e80001eu, 0x3f9837f6u},
      {0x3ec0001au, 0x3fa5fedcu}, {0x3f000013u, 0x3fb504fcu}, {0x3f20002au, 0x3fc56740u},
      {0x3f40006fu, 0x3fd7453eu}, {0x3f600022u, 0x3feac0dcu},
  };
  for (const auto &sample : captured)
    EXPECT_EQ(util::detail::exp::evaluate(sample[0]), sample[1]) << std::hex << sample[0];

  EXPECT_EQ(util::detail::exp::evaluate(0x7fa12345u), 0x7fe12345u);
  EXPECT_EQ(util::detail::exp::evaluate(0x7fa12345u, false), 0x7fa12345u);
  EXPECT_EQ(std::bit_cast<uint32_t>(util::amdgpu_exp_f32(1.0f)), 0x40000000u);
}

TEST(ExpLogScalarModelTest, LogMatchesPhysicalRdna3AndRdna4) {
  // Near-one row boundaries, intermediate precision, and exact-grid packing.
  const uint32_t captured[][2] = {
      {0x3f000000u, 0xbf800000u}, {0x3f7bffffu, 0xbcba1fa3u}, {0x3f7c0000u, 0xbcba1f74u},
      {0x3f7c0001u, 0xbcba1f46u}, {0x3f7dffecu, 0xbc396b23u}, {0x3f7dffffu, 0xbc39643au},
      {0x3f7e0000u, 0xbc3963ddu}, {0x3f7e0001u, 0xbc396380u}, {0x3f7ff5cbu, 0xb96ba0e4u},
      {0x3f7ffffdu, 0xb48a7faeu}, {0x3f7ffffeu, 0xb438aa3cu}, {0x3f7fffffu, 0xb3b8aa3cu},
      {0x3f800000u, 0x00000000u}, {0x3f800001u, 0x3438aa3bu}, {0x3f800005u, 0x3566d4c6u},
      {0x3f800006u, 0x358a7faau}, {0x3f800043u, 0x37415204u}, {0x3f800c30u, 0x3a0ca2fau},
      {0x3f80ffffu, 0x3c37f1ceu}, {0x3f810000u, 0x3c37f286u}, {0x3f810001u, 0x3c37f33du},
      {0x3f81ffffu, 0x3cb73c5au}, {0x3f820000u, 0x3cb73cb4u}, {0x3f820001u, 0x3cb73d0fu},
      {0x3f835d48u, 0x3d19508bu}, {0x3f83ffffu, 0x3d35d66fu}, {0x3f840000u, 0x3d35d69cu},
      {0x3f85ffffu, 0x3d8759afu}, {0x3f860000u, 0x3d8759c5u}, {0x3f860001u, 0x3d8759dbu},
      {0x3fffffffu, 0x3f7fffffu},
  };
  for (const auto &sample : captured)
    EXPECT_EQ(util::detail::log::evaluate(sample[0]), sample[1]) << std::hex << sample[0];

  EXPECT_EQ(util::detail::log::evaluate(0x7fa12345u), 0x7fe12345u);
  EXPECT_EQ(util::detail::log::evaluate(0x7fa12345u, false), 0x7fa12345u);
  EXPECT_EQ(util::detail::log::evaluate(0x80000001u), 0xff800000u);
  EXPECT_EQ(std::bit_cast<uint32_t>(util::amdgpu_log_f32(1.0f)), 0u);
}

TEST(ExpLogScalarModelTest, ExpCompleteFractionHardwareDigests) {
  // FNV digests of independent gfx1100/gfx1201 captures, across both signed
  // unit intervals and all polynomial segments.
  const uint64_t expected[] = {0x68959bb684f42e5full, 0x88b75684494db8d3ull};
  for (uint32_t sign = 0; sign < 2; ++sign) {
    uint64_t digest = 14695981039346656037ull;
    for (uint32_t index = 0; index < (1u << 24); ++index) {
      const float input = static_cast<float>(index) * 0x1p-24f;
      const uint32_t bits = std::bit_cast<uint32_t>(input) | (sign << 31);
      digest = (digest ^ util::detail::exp::evaluate(bits)) * 1099511628211ull;
    }
    EXPECT_EQ(digest, expected[sign]) << "sign=" << sign;
  }
}

TEST(ExpLogScalarModelTest, LogCompleteMantissaHardwareDigest) {
  // Every input in [0.5, 2), including the near-one paths.
  uint64_t digest = 14695981039346656037ull;
  for (uint32_t bits = 0x3f000000u; bits < 0x40000000u; ++bits)
    digest = (digest ^ util::detail::log::evaluate(bits)) * 1099511628211ull;
  EXPECT_EQ(digest, 0xc33a54efbae098beull);
}

TEST(TranscendentalTest, SinCosF32SpecialCases) {
  // sin(2*pi*0) = 0, cos(2*pi*0) = 1
  EXPECT_NEAR(sin_f32(0.0f), 0.0f, 1e-6f);
  EXPECT_NEAR(cos_f32(0.0f), 1.0f, 1e-6f);
  // sin(2*pi*0.25) = 1, cos(2*pi*0.25) = 0
  EXPECT_NEAR(sin_f32(0.25f), 1.0f, 1e-6f);
  EXPECT_NEAR(cos_f32(0.25f), 0.0f, 1e-6f);
  // NaN/Inf inputs
  EXPECT_TRUE(std::isnan(sin_f32(std::numeric_limits<float>::infinity())));
  EXPECT_TRUE(std::isnan(cos_f32(std::numeric_limits<float>::infinity())));
}

TEST(TranscendentalTest, RcpF64SpecialCases) {
  EXPECT_EQ(rcp_f64(0.0), std::numeric_limits<double>::infinity());
  EXPECT_EQ(rcp_f64(-0.0), -std::numeric_limits<double>::infinity());
  EXPECT_DOUBLE_EQ(rcp_f64(2.0), 0.5);
}

TEST(TranscendentalTest, SqrtF64SpecialCases) {
  EXPECT_TRUE(std::isnan(sqrt_f64(-1.0)));
  EXPECT_DOUBLE_EQ(sqrt_f64(4.0), 2.0);
}

// ---------------------------------------------------------------------------
// ULP accuracy tests (pseudorandom inputs)
// ---------------------------------------------------------------------------

TEST(TranscendentalTest, RcpF32Ulp) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(0.001f, 1000.0f);
  for (int i = 0; i < 10000; ++i) {
    float x = dist(rng);
    float result = rcp_f32(x);
    float expected = 1.0f / x;
    // Allow 1 ULP difference
    ASSERT_NEAR(result, expected, std::abs(expected) * 1.2e-7f)
        << "rcp_f32(" << x << ") = " << result << " expected " << expected;
  }
}

TEST(TranscendentalTest, SqrtF32Ulp) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(0.0f, 1e6f);
  for (int i = 0; i < 10000; ++i) {
    float x = dist(rng);
    float result = sqrt_f32(x);
    float expected = std::sqrt(x);
    ASSERT_NEAR(result, expected, std::abs(expected) * 1.2e-7f)
        << "sqrt_f32(" << x << ") = " << result << " expected " << expected;
  }
}

} // namespace
