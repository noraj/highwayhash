// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAYHASH_SCALAR_SIP_HASH_H_
#define HIGHWAYHASH_SCALAR_SIP_HASH_H_

// Scalar (non-vector/SIMD) version for comparison purposes.

#include <cstddef>
#include <cstdint>
#include <cstring>  // memcpy
#include "code_annotation.h"
#include "state_helpers.h"

// Paper: https://www.131002.net/siphash/siphash.pdf
class ScalarSipHashState {
 public:
  using Key = uint64_t[2];
  static const size_t kPacketSize = sizeof(uint64_t);

  explicit INLINE ScalarSipHashState(const Key& key) {
    v0 = 0x736f6d6570736575ull ^ key[0];
    v1 = 0x646f72616e646f6dull ^ key[1];
    v2 = 0x6c7967656e657261ull ^ key[0];
    v3 = 0x7465646279746573ull ^ key[1];
  }

  INLINE void Update(const uint8_t* bytes) {
    uint64_t packet;
    memcpy(&packet, bytes, sizeof(packet));

    v3 ^= packet;

    Compress<2>();

    v0 ^= packet;
  }

  INLINE uint64_t Finalize() {
    // Mix in bits to avoid leaking the key if all packets were zero.
    v2 ^= 0xFF;

    Compress<4>();

    return (v0 ^ v1) ^ (v2 ^ v3);
  }

 private:
  // Rotate a 64-bit value "v" left by N bits.
  template <uint64_t bits>
  static INLINE uint64_t RotateLeft(const uint64_t v) {
    const uint64_t left = v << bits;
    const uint64_t right = v >> (64 - bits);
    return left | right;
  }

  template <size_t rounds>
  INLINE void Compress() {
    for (size_t i = 0; i < rounds; ++i) {
      // ARX network: add, rotate, exclusive-or.
      v0 += v1;
      v2 += v3;
      v1 = RotateLeft<13>(v1);
      v3 = RotateLeft<16>(v3);
      v1 ^= v0;
      v3 ^= v2;

      v0 = RotateLeft<32>(v0);

      v2 += v1;
      v0 += v3;
      v1 = RotateLeft<17>(v1);
      v3 = RotateLeft<21>(v3);
      v1 ^= v2;
      v3 ^= v0;

      v2 = RotateLeft<32>(v2);
    }
  }

  uint64_t v0;
  uint64_t v1;
  uint64_t v2;
  uint64_t v3;
};

// Fast, cryptographically strong pseudo-random function. Useful for:
// . hash tables holding attacker-controlled data. This function is
//   immune to hash flooding DOS attacks because multi-collisions are
//   infeasible to compute, provided the key remains secret.
// . deterministic/idempotent 'random' number generation, e.g. for
//   choosing a subset of items based on their contents.
//
// Robust versus timing attacks because memory accesses are sequential
// and the algorithm is branch-free. Compute time is proportional to the
// number of 8-byte packets and about twice as fast as an sse41 implementation.
//
// "key" is a secret 128-bit key unknown to attackers.
// "bytes" is the data to hash; ceil(size / 8) * 8 bytes are read.
// Returns a 64-bit hash of the given data bytes.
static INLINE uint64_t ScalarSipHash(const ScalarSipHashState::Key& key,
                                     const uint8_t* bytes,
                                     const uint64_t size) {
  return ComputeHash<ScalarSipHashState>(key, bytes, size);
}

template <int kNumLanes>
static INLINE uint64_t ReduceSipTreeHash(const ScalarSipHashState::Key& key,
                                         const uint64_t (&hashes)[kNumLanes]) {
  ScalarSipHashState state(key);

  for (int i = 0; i < kNumLanes; ++i) {
    state.Update(reinterpret_cast<const uint8_t*>(&hashes[i]));
  }

  return state.Finalize();
}

#endif  // #ifndef HIGHWAYHASH_SCALAR_SIP_HASH_H_