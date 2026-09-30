// Copyright Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

// HMAC-SHA-256 over the in-tree SHA-256 (sha256.h). One code path on every
// platform; only the CSPRNG and the key-source scan below are
// platform-specific.

#include "hmac.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>

#include "cuid_util.h"
#include "rocm/sha2/log.h"
#include "rocm/sha2/sha256.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// bcrypt.h must follow windows.h.
#include <bcrypt.h>
#else
#include <dirent.h>
#include <unistd.h>
// getrandom(2) needs glibc >= 2.25 (or musl); where it is missing, and where
// the syscall itself is missing (pre-3.17 kernels, some containers/seccomp
// profiles), fill_random() falls back to reading /dev/urandom. Define
// AMDCUID_HAVE_GETRANDOM=0 on the command line to force the fallback path.
#if !defined(AMDCUID_HAVE_GETRANDOM) && defined(__has_include)
#if __has_include(<sys/random.h>)
#define AMDCUID_HAVE_GETRANDOM 1
#endif
#endif
#if AMDCUID_HAVE_GETRANDOM
#include <sys/random.h>
#endif
#endif

namespace {

// rocm::sha2 has no logging dependency of its own; it reports diagnostics
// (e.g. update() after finalize(), which should never happen in this library)
// through a swappable handler that writes straight to stderr by default. Route
// it into cuid's own Logger instead, so it is subject to the same level
// filtering as the rest of the library and downstream apps aren't surprised by
// unconditional stderr output from a dependency they don't call directly.
void sha2_log_handler(const char* message) { LOG(ERROR, message); }

// Idempotent; called from every cuid_hmac constructor so the handler is
// installed before any sha256 use regardless of construction order.
void init_sha2_logging() {
  static std::once_flag once;
  std::call_once(once, [] { rocm::sha2::set_log_handler(&sha2_log_handler); });
}

// The only digest CUID uses. A wider one would overrun the caller's 32-byte
// output buffer, so set_hmac_algorithm() rejects everything else.
bool is_sha256_name(const char* name) {
  if (!name) return true;  // nullptr means "the default", which is SHA-256
  return std::strcmp(name, "SHA256") == 0 || std::strcmp(name, "SHA-256") == 0 ||
         std::strcmp(name, "sha256") == 0 || std::strcmp(name, "sha-256") == 0;
}

// Fill buf with cryptographically secure random bytes.
bool fill_random(uint8_t* buf, size_t len) {
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(buf),
                                        static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#else
#if AMDCUID_HAVE_GETRANDOM
  size_t off = 0;
  while (off < len) {
    ssize_t n = getrandom(buf + off, len - off, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;  // ENOSYS on a pre-3.17 kernel; fall through to /dev/urandom
    }
    off += static_cast<size_t>(n);
  }
  if (off == len) return true;
#endif
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (!urandom) return false;
  urandom.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
  return urandom.gcount() == static_cast<std::streamsize>(len);
#endif
}

#if !defined(_WIN32)
// Read up to `len` bytes of `path`. Returns the number of bytes read, or -1
// on any error, including a file longer than `len`.
ssize_t read_whole_file(const std::string& path, uint8_t* buf, size_t len) {
  const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) return -1;
  size_t got = 0;
  while (got < len) {
    const ssize_t n = read(fd, buf + got, len - got);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1;
    }
    if (n == 0) break;
    got += static_cast<size_t>(n);
  }
  uint8_t probe;
  const ssize_t extra = read(fd, &probe, 1);
  close(fd);
  if (extra != 0) return -1;
  return static_cast<ssize_t>(got);
}

// A device without cuid_seed, one this process cannot read and one that
// fails with ENODATA are all skipped: amdgpu holds one key for every device.
bool scan_cuid_seed_devices(uint8_t out_key[key_length]) {
  constexpr char kDevicesDir[] = "/sys/bus/pci/devices";
  DIR* dir = opendir(kDevicesDir);
  if (!dir) return false;

  bool found = false;
  struct dirent* entry;
  // This call site owns its DIR*, which is all POSIX requires; readdir_r is
  // deprecated and must not be adopted.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  while (!found && (entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.') continue;
    const std::string base = std::string(kDevicesDir) + "/" + entry->d_name;
    uint8_t bytes[key_length];
    const ssize_t got = read_whole_file(base + "/cuid_seed", bytes, sizeof(bytes));
    if (got != static_cast<ssize_t>(key_length)) {
      rocm::sha2::secure_zero(bytes, sizeof(bytes));
      continue;
    }

    std::memcpy(out_key, bytes, key_length);
    rocm::sha2::secure_zero(bytes, sizeof(bytes));
    found = true;
  }
  closedir(dir);
  return found;
}
#endif  // !_WIN32

}  // namespace

std::recursive_mutex& cuid_operation_mutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

cuid_hmac::cuid_hmac() : key(nullptr), key_len(key_length), valid(false) { init_sha2_logging(); }

void cuid_hmac::reload_key() {
  uint8_t candidate[key_length]{};
  bool found = false;
#if !defined(_WIN32)
  if (geteuid() == 0) found = scan_cuid_seed_devices(candidate);
#endif

  std::lock_guard<std::mutex> lock(key_mutex_);
  if (key) {
    rocm::sha2::secure_zero(key, key_len);
    delete[] key;
    key = nullptr;
  }
  valid = false;
  key_len = key_length;
  if (found) {
    key = new uint8_t[key_length];
    std::memcpy(key, candidate, key_length);
    valid = true;
  }
  rocm::sha2::secure_zero(candidate, sizeof(candidate));
}

cuid_hmac::cuid_hmac(uint8_t key_data[key_length])
    : key(nullptr), key_len(key_length), valid(false) {
  init_sha2_logging();
  key = new uint8_t[key_length];
  std::memcpy(key, key_data, key_length);

  valid = true;
}

cuid_hmac::cuid_hmac(const char* key_data, size_t len) : key(nullptr), key_len(len), valid(false) {
  init_sha2_logging();
  if (!key_data || len == 0) {
    key_len = key_length;
    return;  // leaves valid == false
  }

  key = new uint8_t[len];
  std::memcpy(key, key_data, len);

  valid = true;
}

cuid_hmac::~cuid_hmac() {
  if (key) {
    rocm::sha2::secure_zero(key, key_len);
    delete[] key;
  }
}

amdcuid_status_t cuid_hmac::generate_hmac_sha256(const uint8_t* data, size_t data_len,
                                                 uint8_t* out_hash, size_t* out_len) {
  if (!out_hash) return AMDCUID_STATUS_HMAC_ERROR;

  std::lock_guard<std::mutex> lock(key_mutex_);
  if (!key) {
    LOG(ERROR, "No HMAC key is set");
    return AMDCUID_STATUS_KEY_ERROR;
  }

  rocm::sha2::hmac_sha256(key, key_len, data, data_len, out_hash);
  if (out_len) *out_len = rocm::sha2::SHA256_DIGEST_SIZE;

  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t cuid_hmac::get_key_info(amdcuid_key_info_t* info) const {
  if (!info) return AMDCUID_STATUS_INVALID_ARGUMENT;

  // One lock_guard for the whole read, so a concurrent reload_key()/
  // set_hmac_key() can't land mid-read; the key is copied out so hashing runs
  // outside the lock.
  uint8_t key_copy[key_length];
  size_t key_copy_len;
  std::memset(info, 0, sizeof(*info));
  {
    std::lock_guard<std::mutex> lock(key_mutex_);
    if (!key || !valid) return AMDCUID_STATUS_SUCCESS;
    if (key_len > sizeof(key_copy)) return AMDCUID_STATUS_KEY_ERROR;
    key_copy_len = key_len;
    std::memcpy(key_copy, key, key_copy_len);
  }

  uint8_t digest[32];
  const amdcuid_status_t status = CuidUtilities::sha256_unkeyed(key_copy, key_copy_len, digest);
  rocm::sha2::secure_zero(key_copy, sizeof(key_copy));
  if (status != AMDCUID_STATUS_SUCCESS) return status;

  std::memcpy(info->fingerprint, digest, sizeof(info->fingerprint));
  rocm::sha2::secure_zero(digest, sizeof(digest));
  info->provisioned = 1;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t cuid_hmac::set_hmac_algorithm(const char* digest_name) {
  if (!is_sha256_name(digest_name)) {
    LOG(ERROR, "Unsupported digest: " << digest_name << " (only SHA-256 is supported)");
    return AMDCUID_STATUS_HMAC_ERROR;
  }
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t cuid_hmac::set_hmac_key(const uint8_t key_data[key_length]) {
  if (!key_data) return AMDCUID_STATUS_INVALID_ARGUMENT;

  std::lock_guard<std::mutex> lock(key_mutex_);
  if (key) {
    rocm::sha2::secure_zero(key, key_len);
    delete[] key;
  }
  key = new uint8_t[key_length];
  key_len = key_length;
  std::memcpy(key, key_data, key_length);
  valid = true;

  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t cuid_hmac::generate_key(uint8_t out_key[key_length]) {
  if (!out_key) return AMDCUID_STATUS_INVALID_ARGUMENT;

  if (!fill_random(out_key, key_length)) {
    LOG(ERROR, "Error generating random bytes for HMAC key");
    return AMDCUID_STATUS_KEY_ERROR;
  }

  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidUtilities::sha256_unkeyed(const uint8_t* data, size_t data_len,
                                               uint8_t out[32]) {
  if (!out || (!data && data_len > 0)) return AMDCUID_STATUS_INVALID_ARGUMENT;
  rocm::sha2::sha256_digest(data, data_len, out);
  return AMDCUID_STATUS_SUCCESS;
}

bool CuidUtilities::is_rejected_key(const uint8_t key[key_length]) {
  bool all_equal = true;
  for (size_t i = 1; i < key_length; ++i) all_equal = all_equal && key[i] == key[0];
  if (all_equal) return true;

  const auto padded_equals = [key](const char* text) {
    uint8_t padded[key_length] = {};
    std::memcpy(padded, text, std::strlen(text));
    return std::memcmp(key, padded, key_length) == 0;
  };
  if (padded_equals("AMD-CUID-DEFAULT-SEED-v1") || padded_equals("AMD-CUID-TEMP-KEY-v1"))
    return true;

  // The two conformance-vector keys: 00..1f and 0xa5 ^ n.
  uint8_t counting[key_length];
  uint8_t xored[key_length];
  for (size_t i = 0; i < key_length; ++i) {
    counting[i] = static_cast<uint8_t>(i);
    xored[i] = static_cast<uint8_t>(0xA5 ^ i);
  }
  return std::memcmp(key, counting, key_length) == 0 || std::memcmp(key, xored, key_length) == 0;
}
