#ifndef MLK_SG25_FIPS202_H
#define MLK_SG25_FIPS202_H

#include <stddef.h>
#include <stdint.h>

#if defined(PQC_KECCAK_KROUND25)
#include <pqc/vx_kround25.h>
#elif !defined(PQC_KECCAK_SG25_SW)
#include <pqc/vx_ksg25.h>
#endif
#include <vx_intrinsics.h>

#include "mlk_width_counters.h"

// The surrounding ML-KEM runs on lane 0. Each sponge entry activates the warp,
// broadcasts lane 0's arguments, and leaves one Keccak word in lanes 0..24.
// Incremental SHAKE stores those words in shared memory between API calls.

#define SHAKE128_RATE 168
#define SHAKE256_RATE 136
#define SHA3_256_RATE 136
#define SHA3_384_RATE 104
#define SHA3_512_RATE 72

typedef struct {
  uint64_t ctx[32];
} MLK_ALIGN mlk_shake128ctx;

#if defined(PQC_KECCAK_SG25_SW)
static const uint8_t mlksg_rho[25] = {
  0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
  25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
};

static const uint64_t mlksg_round_constants[24] = {
  UINT64_C(0x0000000000000001), UINT64_C(0x0000000000008082),
  UINT64_C(0x800000000000808a), UINT64_C(0x8000000080008000),
  UINT64_C(0x000000000000808b), UINT64_C(0x0000000080000001),
  UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008009),
  UINT64_C(0x000000000000008a), UINT64_C(0x0000000000000088),
  UINT64_C(0x0000000080008009), UINT64_C(0x000000008000000a),
  UINT64_C(0x000000008000808b), UINT64_C(0x800000000000008b),
  UINT64_C(0x8000000000008089), UINT64_C(0x8000000000008003),
  UINT64_C(0x8000000000008002), UINT64_C(0x8000000000000080),
  UINT64_C(0x000000000000800a), UINT64_C(0x800000008000000a),
  UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008080),
  UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008008),
};

static MLK_INLINE uint64_t mlksg_shuffle(uint64_t value, unsigned source) {
  uint32_t lo = vx_shfl_idx((uint32_t)value, source, 31, 0);
  uint32_t hi = vx_shfl_idx((uint32_t)(value >> 32), source, 31, 0);
  return ((uint64_t)hi << 32) | lo;
}

static MLK_INLINE uint64_t mlksg_rotate(uint64_t value, unsigned shift) {
  return (value << shift) | (value >> ((64 - shift) & 63));
}
#endif

static MLK_INLINE uint32_t mlksg_broadcast_lane0(uint32_t value) {
  return vx_shfl_idx(value, 0, 31, 0);
}

static MLK_INLINE uint8_t *mlksg_broadcast_output(uint8_t *output) {
  return (uint8_t *)(uintptr_t)mlksg_broadcast_lane0(
      (uint32_t)(uintptr_t)output);
}

static MLK_INLINE const uint8_t *mlksg_broadcast_input(
    const uint8_t *input) {
  return (const uint8_t *)(uintptr_t)mlksg_broadcast_lane0(
      (uint32_t)(uintptr_t)input);
}

static MLK_INLINE mlk_shake128ctx *mlksg_broadcast_state(
    mlk_shake128ctx *state) {
  return (mlk_shake128ctx *)(uintptr_t)mlksg_broadcast_lane0(
      (uint32_t)(uintptr_t)state);
}

#if defined(PQC_KECCAK_KROUND25)
#define MLKSG_KROUND_STEP(round) do {                                     \
  const uint32_t lo = (uint32_t)a;                                       \
  const uint32_t hi = (uint32_t)(a >> 32);                               \
  const uint32_t out_lo = vx_kround_l_sg25(lo, hi, round);               \
  const uint32_t out_hi = vx_kround_h_sg25(lo, hi, round);               \
  a = ((uint64_t)out_hi << 32) | out_lo;                                 \
} while (0)
#endif

static __attribute__((noinline)) uint64_t mlksg_permute(uint64_t a) {
#if defined(PQC_KECCAK_KROUND25)
  MLKSG_KROUND_STEP(0);  MLKSG_KROUND_STEP(1);
  MLKSG_KROUND_STEP(2);  MLKSG_KROUND_STEP(3);
  MLKSG_KROUND_STEP(4);  MLKSG_KROUND_STEP(5);
  MLKSG_KROUND_STEP(6);  MLKSG_KROUND_STEP(7);
  MLKSG_KROUND_STEP(8);  MLKSG_KROUND_STEP(9);
  MLKSG_KROUND_STEP(10); MLKSG_KROUND_STEP(11);
  MLKSG_KROUND_STEP(12); MLKSG_KROUND_STEP(13);
  MLKSG_KROUND_STEP(14); MLKSG_KROUND_STEP(15);
  MLKSG_KROUND_STEP(16); MLKSG_KROUND_STEP(17);
  MLKSG_KROUND_STEP(18); MLKSG_KROUND_STEP(19);
  MLKSG_KROUND_STEP(20); MLKSG_KROUND_STEP(21);
  MLKSG_KROUND_STEP(22); MLKSG_KROUND_STEP(23);
#else
  unsigned round;
#if defined(PQC_KECCAK_SG25_SW)
  const unsigned lane = (unsigned)vx_thread_id();
  const unsigned t = lane % 25;
  const unsigned x = t % 5;
  const unsigned y = t / 5;
#endif
#pragma clang loop unroll(disable)
  for (round = 0; round < 24; ++round) {
#if defined(PQC_KECCAK_SG25_SW)
    const uint64_t pair = a ^ mlksg_shuffle(a, x + 5 * ((y + 1) % 5));
    const uint64_t four = pair ^ mlksg_shuffle(pair, x + 5 * ((y + 2) % 5));
    const uint64_t column = four ^ mlksg_shuffle(a, x + 5 * ((y + 4) % 5));
    a ^= mlksg_shuffle(column, (x + 4) % 5)
       ^ mlksg_rotate(mlksg_shuffle(column, (x + 1) % 5), 1);
    const uint64_t rotated = mlksg_rotate(a, mlksg_rho[t]);
    const uint64_t b = mlksg_shuffle(rotated,
                                     (x + 3 * y) % 5 + 5 * x);
    a = b ^ (~mlksg_shuffle(b, (x + 1) % 5 + 5 * y)
           & mlksg_shuffle(b, (x + 2) % 5 + 5 * y));
    const uint64_t mask = UINT64_C(0) - (uint64_t)(lane == 0);
    a ^= mlksg_round_constants[round] & mask;
#else
    uint32_t alo = (uint32_t)a;
    uint32_t ahi = (uint32_t)(a >> 32);
    uint32_t tlo = vx_ktheta_l_sg25(alo, ahi);
    uint32_t thi = vx_ktheta_h_sg25(alo, ahi);
    uint32_t blo = vx_krhopi_l_sg25(tlo, thi);
    uint32_t bhi = vx_krhopi_h_sg25(tlo, thi);
    alo = vx_kchii_l_sg25(blo, round);
    ahi = vx_kchii_h_sg25(bhi, round);
    a = ((uint64_t)ahi << 32) | alo;
#endif
  }
#endif
  return a;
}

#if defined(PQC_KECCAK_KROUND25)
#undef MLKSG_KROUND_STEP
#endif

static MLK_INLINE uint64_t mlksg_load_word(const uint8_t *input,
                                            size_t length, unsigned lane) {
  uint64_t value = 0;
  size_t offset = 8u * lane;
  unsigned i;
  for (i = 0; i < 8 && offset + i < length; ++i) {
    value |= (uint64_t)input[offset + i] << (8 * i);
  }
  return value;
}

static MLK_INLINE void mlksg_extract(uint8_t *output, size_t length,
                                     uint64_t lane_value) {
  const unsigned lane = (unsigned)vx_thread_id();
  size_t offset = 8u * lane;
  if (offset < length) {
    unsigned i;
    for (i = 0; i < 8 && offset + i < length; ++i) {
      output[offset + i] = (uint8_t)(lane_value >> (8 * i));
    }
  }
}

static MLK_INLINE void mlksg_count(unsigned permutations) {
  if (vx_thread_id() == 0) {
    unsigned wid = (unsigned)vx_warp_id() & (MLKW_MAX_WARPS - 1);
    mlkw_counts[wid][MLKW_KECCAK_X1] += permutations;
    mlkw_counts[wid][MLKW_SLOTS] += permutations;
  }
}

static __attribute__((noinline)) void mlksg_sponge_worker(
    uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen,
    unsigned rate, uint8_t domain) {
  output = mlksg_broadcast_output(output);
  outlen = mlksg_broadcast_lane0((uint32_t)outlen);
  input = mlksg_broadcast_input(input);
  inlen = mlksg_broadcast_lane0((uint32_t)inlen);
  rate = mlksg_broadcast_lane0(rate);
  domain = (uint8_t)mlksg_broadcast_lane0(domain);

  const unsigned lane = (unsigned)vx_thread_id();
  uint64_t a = 0;
  unsigned permutations = 0;

  while (inlen >= rate) {
    a ^= mlksg_load_word(input, rate, lane);
    a = mlksg_permute(a);
    ++permutations;
    input += rate;
    inlen -= rate;
  }

  a ^= mlksg_load_word(input, inlen, lane);
  if (lane == inlen / 8) {
    a ^= (uint64_t)domain << (8 * (inlen % 8));
  }
  if (lane == rate / 8 - 1) {
    a ^= UINT64_C(0x8000000000000000);
  }

  while (outlen != 0) {
    size_t length = outlen < rate ? outlen : rate;
    a = mlksg_permute(a);
    ++permutations;
    mlksg_extract(output, length, a);
    output += length;
    outlen -= length;
  }
  vx_fence();
  mlksg_count(permutations);
}

static __attribute__((noinline)) void mlksg_sponge(
    uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen,
    unsigned rate, uint8_t domain) {
  vx_tmc(-1);
  mlksg_sponge_worker(output, outlen, input, inlen, rate, domain);
  vx_tmc_one();
}

#define mlk_shake128_init MLK_NAMESPACE(shake128_init)
static MLK_INLINE void mlk_shake128_init(mlk_shake128ctx *state) {
  (void)state;
}

#define mlk_shake128_absorb_once MLK_NAMESPACE(shake128_absorb_once)
static __attribute__((noinline)) void mlksg_shake128_absorb_once_worker(
    mlk_shake128ctx *state, const uint8_t *input, size_t inlen) {
  state = mlksg_broadcast_state(state);
  input = mlksg_broadcast_input(input);
  inlen = mlksg_broadcast_lane0((uint32_t)inlen);

  const unsigned lane = (unsigned)vx_thread_id();
  uint64_t a = 0;
  unsigned permutations = 0;

  while (inlen >= SHAKE128_RATE) {
    a ^= mlksg_load_word(input, SHAKE128_RATE, lane);
    a = mlksg_permute(a);
    ++permutations;
    input += SHAKE128_RATE;
    inlen -= SHAKE128_RATE;
  }
  a ^= mlksg_load_word(input, inlen, lane);
  if (lane == inlen / 8) {
    a ^= UINT64_C(0x1f) << (8 * (inlen % 8));
  }
  if (lane == SHAKE128_RATE / 8 - 1) {
    a ^= UINT64_C(0x8000000000000000);
  }
  state->ctx[lane] = a;
  mlksg_count(permutations);
}

static MLK_INLINE void mlk_shake128_absorb_once(
    mlk_shake128ctx *state, const uint8_t *input, size_t inlen) {
  vx_tmc(-1);
  mlksg_shake128_absorb_once_worker(state, input, inlen);
  vx_tmc_one();
}

#define mlk_shake128_squeezeblocks MLK_NAMESPACE(shake128_squeezeblocks)
static __attribute__((noinline)) void mlksg_shake128_squeezeblocks_worker(
    uint8_t *output, size_t nblocks, mlk_shake128ctx *state) {
  output = mlksg_broadcast_output(output);
  nblocks = mlksg_broadcast_lane0((uint32_t)nblocks);
  state = mlksg_broadcast_state(state);

  const unsigned lane = (unsigned)vx_thread_id();
  uint64_t a = state->ctx[lane];
  size_t block;
  for (block = 0; block < nblocks; ++block) {
    a = mlksg_permute(a);
    mlksg_extract(output, SHAKE128_RATE, a);
    output += SHAKE128_RATE;
  }
  state->ctx[lane] = a;
  vx_fence();
  mlksg_count((unsigned)nblocks);
}

static MLK_INLINE void mlk_shake128_squeezeblocks(
    uint8_t *output, size_t nblocks, mlk_shake128ctx *state) {
  vx_tmc(-1);
  mlksg_shake128_squeezeblocks_worker(output, nblocks, state);
  vx_tmc_one();
}

static __attribute__((noinline)) void mlksg_shake128_release_worker(
    mlk_shake128ctx *state) {
  state = mlksg_broadcast_state(state);
  state->ctx[(unsigned)vx_thread_id()] = 0;
  vx_fence();
}

#define mlk_shake128_release MLK_NAMESPACE(shake128_release)
static MLK_INLINE void mlk_shake128_release(mlk_shake128ctx *state) {
  vx_tmc(-1);
  mlksg_shake128_release_worker(state);
  vx_tmc_one();
}

#define mlk_shake256 MLK_NAMESPACE(shake256)
static MLK_INLINE void mlk_shake256(uint8_t *output, size_t outlen,
                                    const uint8_t *input, size_t inlen) {
  mlksg_sponge(output, outlen, input, inlen, SHAKE256_RATE, 0x1f);
}

#define SHA3_256_HASHBYTES 32
#define mlk_sha3_256 MLK_NAMESPACE(sha3_256)
static MLK_INLINE void mlk_sha3_256(uint8_t *output, const uint8_t *input,
                                    size_t inlen) {
  mlksg_sponge(output, SHA3_256_HASHBYTES, input, inlen, SHA3_256_RATE, 0x06);
}

#define SHA3_512_HASHBYTES 64
#define mlk_sha3_512 MLK_NAMESPACE(sha3_512)
static MLK_INLINE void mlk_sha3_512(uint8_t *output, const uint8_t *input,
                                    size_t inlen) {
  mlksg_sponge(output, SHA3_512_HASHBYTES, input, inlen, SHA3_512_RATE, 0x06);
}

#endif
