/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#include "memcpy_performance_common.hh"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/**
 * @addtogroup memcpy memcpy
 * @{
 * @ingroup PerformanceTestMemory
 */

namespace {

// Observed on gfx1201: a runtime whose SDMA ring read pointer does not track consumed bytes
// wedges this workload at ~54,000 copies, so that is one ring's worth here. Nothing is assumed
// about how many ring bytes a copy costs -- that is a property of ROCr's packet emission for
// this copy shape and arch, not a fixed quantity, so it is deliberately not encoded.
constexpr long long kObservedStallCopies = 54000;
constexpr size_t kRingWrapBufBytes = 64 * 1024;

// Copies issued before the clock starts, so queue creation, first-touch faults and any lazy
// blit setup land outside the measurement.
constexpr int kRingWrapWarmupCopies = 2000;

// One-line, greppable summary so the native SDMA user queue and the legacy queue can be
// compared by running this test twice with HSA_ENABLE_SDMA_USER_QUEUE flipped and diffing it.
void PrintRingWrapTiming(long long copies, double copy_secs) {
  const char* env = std::getenv("HSA_ENABLE_SDMA_USER_QUEUE");
  const char* path = (env == nullptr)        ? "native-user-queue(default)"
                     : (std::atoi(env) != 0) ? "native-user-queue"
                                             : "legacy-sws-queue";
  const double bytes = static_cast<double>(copies) * static_cast<double>(kRingWrapBufBytes);
  const double rate = (copy_secs > 0.0) ? static_cast<double>(copies) / copy_secs : 0.0;
  const double gib_s = (copy_secs > 0.0) ? bytes / copy_secs / (1024.0 * 1024.0 * 1024.0) : 0.0;
  const double us_per_copy = (copies > 0) ? copy_secs * 1e6 / static_cast<double>(copies) : 0.0;

  std::printf(
      "[SDMA-PERF] path=%-26s copies=%lld size=%zuKiB time=%.3fs rate=%.0f copies/s "
      "bw=%.2f GiB/s per_copy=%.2fus\n",
      path, copies, kRingWrapBufBytes / 1024, copy_secs, rate, gib_s, us_per_copy);
  std::fflush(stdout);
}

}  // namespace

/**
 * Test Description
 * ------------------------
 *    - Drives far more device-to-device NoCU traffic through one stream than the SDMA blit
 * ring holds, forcing the ring to wrap several times, and reports the achieved rate.
 *
 * Lives in the performance suite rather than unit: it runs for seconds by design, which is
 * too slow for staging, and its output is a throughput number.
 *
 * NoCU is required, not incidental: it is what puts the copies on the SDMA engine at all.
 * Plain hipMemcpyDeviceToDevice is serviced by the blit kernel and pinned host<->device
 * copies are satisfied above ROCr, so neither reaches the ring this test is about.
 *
 * BlitSdma's producer only consults the ring read pointer in CanWriteUpto():
 *     (upto_index - *queue_rptr_) < kQueueSize
 * Both are monotonic byte counts, so below one ring's worth of traffic the check passes no
 * matter what the read pointer contains -- a stale, wrong or entirely dead read pointer is
 * invisible, and the rest of the test suite never gets far enough to notice.
 *
 * HOW THIS FAILS: past that point a broken read pointer does not corrupt data, it deadlocks.
 * The producer spins forever in BlitSdma::AcquireWriteAddress waiting for ring space the
 * engine has already freed, so the test HANGS rather than returning a wrong answer. ctest's
 * per-test timeout is what reports it; the progress lines show how far it got, and a stall
 * near one ring's worth of copies is the signature.
 *
 * Prints a [SDMA-PERF] line for comparing the native user queue against the legacy queue
 * (HSA_ENABLE_SDMA_USER_QUEUE=1 vs 0). ctest hides stdout for passing tests, so run
 * MemcpyPerformance.exe directly or use ctest -V.
 * ------------------------
 *    - catch\performance\api\memcpy\hipMemcpySdmaRingWrap.cc
 * Test requirements
 * ------------------------
 *    - HIP_VERSION >= 6.1
 */
HIP_TEST_CASE(Performance_hipMemcpy_SdmaRingWrap) {
  // Both settings clear the ~54k-copy stall point by a wide margin.
  const int iters = isQuickLevel() ? 120000 : 250000;

  unsigned char* h_pattern = nullptr;
  unsigned char* h_check = nullptr;
  void* d_src = nullptr;
  void* d_dst = nullptr;

  HIP_CHECK(hipHostMalloc(reinterpret_cast<void**>(&h_pattern), kRingWrapBufBytes));
  HIP_CHECK(hipHostMalloc(reinterpret_cast<void**>(&h_check), kRingWrapBufBytes));
  HIP_CHECK(hipMalloc(&d_src, kRingWrapBufBytes));
  HIP_CHECK(hipMalloc(&d_dst, kRingWrapBufBytes));

  hipStream_t stream;
  HIP_CHECK(hipStreamCreate(&stream));

  for (size_t i = 0; i < kRingWrapBufBytes; ++i)
    h_pattern[i] = static_cast<unsigned char>((i * 7) & 0xff);
  HIP_CHECK(hipMemcpy(d_src, h_pattern, kRingWrapBufBytes, hipMemcpyHostToDevice));
  HIP_CHECK(hipMemset(d_dst, 0, kRingWrapBufBytes));
  HIP_CHECK(hipDeviceSynchronize());

  // Warm up outside the measurement: queue creation and any lazy blit setup would otherwise
  // be charged to the first copies and skew a user-queue vs legacy-queue comparison.
  for (int i = 0; i < kRingWrapWarmupCopies; ++i) {
    HIP_CHECK(
        hipMemcpyAsync(d_dst, d_src, kRingWrapBufBytes, hipMemcpyDeviceToDeviceNoCU, stream));
  }
  HIP_CHECK(hipStreamSynchronize(stream));

  double copy_secs = 0.0;
  auto span_start = std::chrono::steady_clock::now();
  long long copies = 0;
  bool mismatch = false;

  for (int i = 0; i < iters; ++i) {
    HIP_CHECK(
        hipMemcpyAsync(d_dst, d_src, kRingWrapBufBytes, hipMemcpyDeviceToDeviceNoCU, stream));

    // Drain periodically. Without this the stream's own queue grows unboundedly and we would
    // be measuring stream backpressure instead of ring reuse; the ring still wraps because
    // the monotonic indices never reset. Also verifies the data, since a wrong read pointer
    // can corrupt in-flight packets rather than merely stall.
    if ((i % 1024) == 1023) {
      // Close the timed span before verifying and reopen it after: the synchronize and the
      // readback are this test's bookkeeping, not the copy path being measured.
      HIP_CHECK(hipStreamSynchronize(stream));
      copy_secs +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - span_start).count();

      std::memset(h_check, 0, kRingWrapBufBytes);
      HIP_CHECK(hipMemcpy(h_check, d_dst, kRingWrapBufBytes, hipMemcpyDeviceToHost));
      if (std::memcmp(h_check, h_pattern, kRingWrapBufBytes) != 0) {
        mismatch = true;
        break;
      }
      span_start = std::chrono::steady_clock::now();
    }

    ++copies;
    // Breadcrumbs: if a broken read pointer wedges the producer, ctest reports a timeout and
    // the last line printed says how many copies got through.
    if ((copies % 50000) == 0) {
      std::printf("  %lld copies (%.2fx the ~%lld-copy stall point)\n", copies,
                  static_cast<double>(copies) / kObservedStallCopies, kObservedStallCopies);
      std::fflush(stdout);
    }
  }

  HIP_CHECK(hipStreamSynchronize(stream));
  copy_secs +=
      std::chrono::duration<double>(std::chrono::steady_clock::now() - span_start).count();

  HIP_CHECK(hipStreamDestroy(stream));
  HIP_CHECK(hipFree(d_src));
  HIP_CHECK(hipFree(d_dst));
  HIP_CHECK(hipHostFree(h_pattern));
  HIP_CHECK(hipHostFree(h_check));

  PrintRingWrapTiming(copies, copy_secs);

  REQUIRE_FALSE(mismatch);
  REQUIRE(copies == iters);
}

/**
 * End doxygen group PerformanceTestMemory.
 * @}
 */
