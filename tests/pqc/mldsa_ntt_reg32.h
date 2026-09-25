#ifndef MLDSA_NTT_REG32_H
#define MLDSA_NTT_REG32_H

#include <pqc/vx_ntt.h>

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-DSA register NTT requires a complete 32-lane ALU vector");

static inline int32_t mld_ntt_mul(int32_t a, int32_t b) {
#if defined(PQC_NTTMUL_D)
  return vx_nttmul_d(a, b);
#else
  return mld_montgomery_reduce((int64_t)a * b);
#endif
}

static inline int32_t mld_ntt_ct_xor(int32_t value, int32_t zeta,
                                      unsigned distance, unsigned lane) {
#if defined(PQC_NTTBF_D)
  (void)lane;
  switch (distance) {
  case 1: return vx_nttbf_ct_d_xor1(value, zeta);
  case 2: return vx_nttbf_ct_d_xor2(value, zeta);
  case 4: return vx_nttbf_ct_d_xor4(value, zeta);
  case 8: return vx_nttbf_ct_d_xor8(value, zeta);
  case 16: return vx_nttbf_ct_d_xor16(value, zeta);
  default: __builtin_unreachable();
  }
#else
  const int32_t peer = (int32_t)vx_shfl_bfly((uint32_t)value, distance, 31, 0);
  const uint32_t high = 0u - (uint32_t)((lane & distance) != 0);
  const int32_t a = (int32_t)(((uint32_t)value & ~high) | ((uint32_t)peer & high));
  const int32_t b = (int32_t)(((uint32_t)peer & ~high) | ((uint32_t)value & high));
  const int32_t t = mld_ntt_mul(b, zeta);
  int32_t result = (int32_t)(((uint32_t)(a + t) & ~high)
                           | ((uint32_t)(a - t) & high));
  asm volatile ("" : "+r"(result) :: "memory");
  return result;
#endif
}

static inline int32_t mld_ntt_gs_xor(int32_t value, int32_t zeta,
                                      unsigned distance, unsigned lane) {
#if defined(PQC_NTTBF_D)
  (void)lane;
  switch (distance) {
  case 1: return vx_nttbf_gs_d_xor1(value, zeta);
  case 2: return vx_nttbf_gs_d_xor2(value, zeta);
  case 4: return vx_nttbf_gs_d_xor4(value, zeta);
  case 8: return vx_nttbf_gs_d_xor8(value, zeta);
  case 16: return vx_nttbf_gs_d_xor16(value, zeta);
  default: __builtin_unreachable();
  }
#else
  const int32_t peer = (int32_t)vx_shfl_bfly((uint32_t)value, distance, 31, 0);
  const uint32_t high = 0u - (uint32_t)((lane & distance) != 0);
  const int32_t a = (int32_t)(((uint32_t)value & ~high) | ((uint32_t)peer & high));
  const int32_t b = (int32_t)(((uint32_t)peer & ~high) | ((uint32_t)value & high));
  const int32_t product = mld_ntt_mul(a - b, zeta);
  int32_t result = (int32_t)(((uint32_t)(a + b) & ~high)
                           | ((uint32_t)product & high));
  asm volatile ("" : "+r"(result) :: "memory");
  return result;
#endif
}

static __attribute__((noinline)) void mld_ntt_reg32(int32_t* p,
                                                     unsigned inverse) {
  const unsigned lane = vx_thread_id();
  int32_t x[8];
#pragma clang loop unroll(full)
  for (unsigned k = 0; k < 8; ++k)
    x[k] = p[lane + 32 * k];

  if (!inverse) {
#pragma clang loop unroll(full)
    for (unsigned layer = 1; layer <= 8; ++layer) {
      const unsigned len = 256u >> layer;
      if (len >= 32) {
        const unsigned stride = len >> 5;
#pragma clang loop unroll(full)
        for (unsigned k = 0; k < 8; ++k) {
          if (k & stride) continue;
          const unsigned block = (k << 5) / (2 * len);
          const int32_t zeta = mld_zetas[(1u << (layer - 1)) + block];
          const int32_t a = x[k];
          const int32_t t = mld_ntt_mul(x[k + stride], zeta);
          x[k] = a + t;
          x[k + stride] = a - t;
        }
      } else {
#pragma clang loop unroll(full)
        for (unsigned k = 0; k < 8; ++k) {
          const unsigned block = ((k << 5) + lane) / (2 * len);
          const int32_t zeta = mld_zetas[(1u << (layer - 1)) + block];
          x[k] = mld_ntt_ct_xor(x[k], zeta, len, lane);
        }
      }
    }
  } else {
#pragma clang loop unroll(full)
    for (unsigned layer = 8; layer >= 1; --layer) {
      const unsigned len = 256u >> layer;
      if (len < 32) {
#pragma clang loop unroll(full)
        for (unsigned k = 0; k < 8; ++k) {
          const unsigned block = ((k << 5) + lane) / (2 * len);
          const int32_t zeta = -mld_zetas[(1u << layer) - 1 - block];
          x[k] = mld_ntt_gs_xor(x[k], zeta, len, lane);
        }
      } else {
        const unsigned stride = len >> 5;
#pragma clang loop unroll(full)
        for (unsigned k = 0; k < 8; ++k) {
          if (k & stride) continue;
          const unsigned block = (k << 5) / (2 * len);
          const int32_t zeta = -mld_zetas[(1u << layer) - 1 - block];
          const int32_t a = x[k];
          const int32_t b = x[k + stride];
          x[k] = a + b;
          x[k + stride] = mld_ntt_mul(a - b, zeta);
        }
      }
    }
#pragma clang loop unroll(full)
    for (unsigned k = 0; k < 8; ++k)
      x[k] = mld_ntt_mul(x[k], 41978);
  }

#pragma clang loop unroll(full)
  for (unsigned k = 0; k < 8; ++k)
    p[lane + 32 * k] = x[k];
}

#endif
