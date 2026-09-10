#ifndef VORTEX_SG25_REFERENCE_H
#define VORTEX_SG25_REFERENCE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "common.h"

extern "C" {
#include "src/common.h"
#include "src/fips202/keccakf1600.c"
}

namespace sg25_ref {

static const uint64_t round_constants[SG25_ROUNDS] = {
  0x0000000000000001ULL, 0x0000000000008082ULL,
  0x800000000000808aULL, 0x8000000080008000ULL,
  0x000000000000808bULL, 0x0000000080000001ULL,
  0x8000000080008081ULL, 0x8000000000008009ULL,
  0x000000000000008aULL, 0x0000000000000088ULL,
  0x0000000080008009ULL, 0x000000008000000aULL,
  0x000000008000808bULL, 0x800000000000008bULL,
  0x8000000000008089ULL, 0x8000000000008003ULL,
  0x8000000000008002ULL, 0x8000000000000080ULL,
  0x000000000000800aULL, 0x800000008000000aULL,
  0x8000000080008081ULL, 0x8000000000008080ULL,
  0x0000000080000001ULL, 0x8000000080008008ULL
};

inline uint64_t rotate(uint64_t value, unsigned shift) {
  return shift ? (value << shift) | (value >> (64 - shift)) : value;
}

inline void round(uint64_t state[SG25_WORDS], unsigned index) {
  static const unsigned rotations[SG25_WORDS] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
  };
  uint64_t columns[5] = {};
  uint64_t shuffled[SG25_WORDS];
  for (unsigned x = 0; x < 5; ++x) {
    for (unsigned y = 0; y < 5; ++y) {
      columns[x] ^= state[x + 5 * y];
    }
  }
  for (unsigned x = 0; x < 5; ++x) {
    const uint64_t delta = columns[(x + 4) % 5]
                         ^ rotate(columns[(x + 1) % 5], 1);
    for (unsigned y = 0; y < 5; ++y) {
      shuffled[y + 5 * ((2 * x + 3 * y) % 5)] =
          rotate(state[x + 5 * y] ^ delta, rotations[x + 5 * y]);
    }
  }
  for (unsigned y = 0; y < 5; ++y) {
    for (unsigned x = 0; x < 5; ++x) {
      state[x + 5 * y] = shuffled[x + 5 * y]
          ^ (~shuffled[(x + 1) % 5 + 5 * y]
             & shuffled[(x + 2) % 5 + 5 * y]);
    }
  }
  state[0] ^= round_constants[index];
}

inline void shake(const uint8_t* input, size_t input_bytes,
                  uint8_t* output, size_t output_bytes, uint32_t rate) {
  uint64_t state[SG25_WORDS] = {};
  while (input_bytes >= rate) {
    for (unsigned i = 0; i < rate; ++i) {
      state[i / 8] ^= uint64_t(input[i]) << (8 * (i % 8));
    }
    mlk_keccakf1600_permute(state);
    input += rate;
    input_bytes -= rate;
  }
  for (size_t i = 0; i < input_bytes; ++i) {
    state[i / 8] ^= uint64_t(input[i]) << (8 * (i % 8));
  }
  state[input_bytes / 8] ^= uint64_t(0x1f) << (8 * (input_bytes % 8));
  state[(rate - 1) / 8] ^= uint64_t(0x80) << (8 * ((rate - 1) % 8));
  while (output_bytes != 0) {
    mlk_keccakf1600_permute(state);
    const size_t count = std::min(output_bytes, size_t(rate));
    for (size_t i = 0; i < count; ++i) {
      output[i] = uint8_t(state[i / 8] >> (8 * (i % 8)));
    }
    output += count;
    output_bytes -= count;
  }
}

struct shake_kat_t {
  uint32_t rate;
  const char* input;
  const char* output_hex;
};

static const shake_kat_t shake_kats[] = {
  {168, "", "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26"},
  {168, "abc", "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8"},
  {136, "", "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"},
  {136, "abc", "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"}
};

} // namespace sg25_ref

#endif
