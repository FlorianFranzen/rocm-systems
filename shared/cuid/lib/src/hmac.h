// Copyright Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT

#ifndef HMAC_H
#define HMAC_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "include/amd_cuid.h"

// Length of a provisioned secret, in bytes. Not a maximum: the specification
// defines a 256-bit shared secret, and any other size is rejected as corrupt.
#define key_length 32
#define hash_length 32

// The node key lives only in amdgpu's memory. The library reads it, as root
// only, from an amdgpu device's cuid_seed, and writes it only in
// amdcuid_set_hash_key(), to cuid_seed.

std::recursive_mutex& cuid_operation_mutex();

// HMAC-SHA-256 over the in-tree SHA-256 (see sha256.h), keyed with the node
// key or a caller-supplied one.
class cuid_hmac {
 private:
  uint8_t* key;
  size_t key_len;
  bool valid;
  // Guards key, key_len and valid against concurrent readers
  // (generate_hmac_sha256, get_key_info) and writers (set_hmac_key,
  // reload_key).
  mutable std::mutex key_mutex_;

 public:
  cuid_hmac();
  cuid_hmac(uint8_t key_data[key_length]);
  // Key of an explicit length; used by tests. Does not read cuid_seed.
  cuid_hmac(const char* key_data, size_t len);
  ~cuid_hmac();
  bool is_valid() const {
    std::lock_guard<std::mutex> lock(key_mutex_);
    return valid;
  }

  amdcuid_status_t generate_hmac_sha256(const uint8_t* data, size_t data_len, uint8_t* out_hash,
                                        size_t* out_len);
  amdcuid_status_t set_hmac_algorithm(const char* digest_name);

  // Replace the in-memory key without touching cuid_seed. Used by tests; the
  // library obtains the node key through reload_key().
  amdcuid_status_t set_hmac_key(const uint8_t key_data[key_length]);

  // Rediscover the node key: the first amdgpu cuid_seed that reads back 32
  // octets, else none. amdgpu fails the read with ENODATA while it holds no
  // key. A non-root caller never has a key, so it gets temporary CUIDs for
  // key-gated components.
  void reload_key();

  // Whether a key is held and the first 8 octets of the unkeyed SHA-256 of
  // it, read under one lock so a concurrent reload cannot mix pre- and
  // post-rekey state.
  amdcuid_status_t get_key_info(amdcuid_key_info_t* info) const;

  amdcuid_status_t generate_key(uint8_t key[key_length]);
};

// Unkeyed SHA-256 digest of data into a 32-byte output buffer.
//
// Namespaced for the same reason cuid::get_hash_from_raw is: this archive is
// linked into libamd_smi.so, where a name this generic at global scope invites
// a collision. Declared here rather than in cuid_util.h to keep hmac.h
// self-contained; the definition is in hmac.cc, beside the rocm::sha2 calls.
namespace CuidUtilities {
amdcuid_status_t sha256_unkeyed(const uint8_t* data, size_t data_len, uint8_t out[32]);

// A key that must not be provisioned: all 32 octets equal, or a public
// constant (AMD-CUID-DEFAULT-SEED-v1, AMD-CUID-TEMP-KEY-v1, the conformance
// vectors' test seed 00..1f) zero-padded to 32 octets.
bool is_rejected_key(const uint8_t key[key_length]);
}  // namespace CuidUtilities

#endif  // HMAC_H
