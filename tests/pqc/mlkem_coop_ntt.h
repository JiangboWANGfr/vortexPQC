#ifndef _MLKEM_COOP_NTT_H_
#define _MLKEM_COOP_NTT_H_

#include <vx_intrinsics.h>
#include <vx_pqc.h>

static inline __attribute__((always_inline)) int16_t mlk_ntt_fqmul(
    int16_t a, int16_t b) {
#if defined(PQC_NTTMUL_K)
  return vx_nttmul_k(a, b);
#else
  return mlk_fqmul(a, b);
#endif
}

// Include after upstream poly.c to share its arithmetic and twiddle table.
// All participating lanes must be active in one warp; lanes divides 128.
static inline void mlk_ntt_coop(int16_t* r, unsigned lanes, unsigned tid) {
  for (unsigned layer = 1; layer <= 7; ++layer) {
    const unsigned lg = 8u - layer;
    const unsigned len = 1u << lg;
    const unsigned k0 = 1u << (layer - 1);
    for (unsigned b = tid; b < MLKEM_N / 2; b += lanes) {
      const unsigned block = b >> lg;
      const unsigned j = (block << (lg + 1)) + (b & (len - 1u));
      const int16_t t = mlk_ntt_fqmul(r[j + len], mlk_zetas[k0 + block]);
      r[j + len] = (int16_t)(r[j] - t);
      r[j] = (int16_t)(r[j] + t);
    }
    // BAR drains pending LSU work so the next layer sees every lane's stores.
    __syncthreads();
  }
}

static inline void mlk_invntt_coop(int16_t* r, unsigned lanes, unsigned tid) {
  for (unsigned j = tid; j < MLKEM_N; j += lanes) {
    r[j] = mlk_ntt_fqmul(r[j], 1441);
  }
  __syncthreads();
  for (unsigned layer = 7; layer > 0; --layer) {
    const unsigned lg = 8u - layer;
    const unsigned len = 1u << lg;
    const unsigned k0 = (1u << layer) - 1u;
    for (unsigned b = tid; b < MLKEM_N / 2; b += lanes) {
      const unsigned block = b >> lg;
      const unsigned j = (block << (lg + 1)) + (b & (len - 1u));
      const int16_t t = r[j];
      r[j] = mlk_barrett_reduce((int16_t)(t + r[j + len]));
      r[j + len] = mlk_ntt_fqmul((int16_t)(r[j + len] - t), mlk_zetas[k0 - block]);
    }
    __syncthreads();
  }
}

#if defined(PQC_NTT_REG32) || defined(PQC_NTT_SMEM32)
static_assert(VX_CFG_NUM_THREADS == 32, "W32 NTT requires 32 threads");
static_assert(VX_CFG_NUM_ALU_LANES == 32, "W32 NTT requires 32 ALU lanes");

static inline __attribute__((always_inline)) void mlk_ntt_ct_local(
    int16_t& a, int16_t& b, int16_t zeta) {
  const int16_t t = mlk_ntt_fqmul(b, zeta);
  b = (int16_t)(a - t);
  a = (int16_t)(a + t);
}

static inline __attribute__((always_inline)) void mlk_ntt_gs_local(
    int16_t& a, int16_t& b, int16_t zeta) {
  const int16_t t = a;
  a = mlk_barrett_reduce((int16_t)(t + b));
  b = mlk_ntt_fqmul((int16_t)(b - t), zeta);
}

// Bound live ranges at the arithmetic boundaries so the eight coefficients stay in GPRs.
static inline __attribute__((always_inline)) int16_t mlk_ntt_keep_i16(int16_t value) {
  asm volatile ("" : "+r"(value) :: "memory");
  return value;
}

static inline __attribute__((always_inline)) int16_t mlk_ntt_zeta(unsigned index) {
  asm volatile ("" : "+r"(index) :: "memory");
  return mlk_zetas[index];
}

#if defined(PQC_NTT_REG32)
static inline __attribute__((always_inline)) int16_t mlk_select_i16(
    int16_t a, int16_t b, uint32_t mask) {
  return (int16_t)(((uint32_t)(uint16_t)a & ~mask)
                 | ((uint32_t)(uint16_t)b & mask));
}

static inline __attribute__((always_inline)) int16_t mlk_ntt_ct_xor(
    int16_t value, int16_t zeta, unsigned distance, unsigned tid) {
#if defined(PQC_NTTBF_K)
  (void)tid;
  switch (distance) {
  case 2: return vx_nttbf_ct_k_xor2(value, zeta);
  case 4: return vx_nttbf_ct_k_xor4(value, zeta);
  case 8: return vx_nttbf_ct_k_xor8(value, zeta);
  case 16: return vx_nttbf_ct_k_xor16(value, zeta);
  default: __builtin_unreachable();
  }
#else
  const int16_t peer = (int16_t)vx_shfl_bfly((uint16_t)value, distance, 31, 0);
  const uint32_t high = 0u - (uint32_t)((tid & distance) != 0);
  const int16_t a = mlk_select_i16(value, peer, high);
  const int16_t b = mlk_select_i16(peer, value, high);
  const int16_t t = mlk_ntt_fqmul(b, zeta);
  int16_t result = mlk_select_i16((int16_t)(a + t), (int16_t)(a - t), high);
  asm volatile ("" : "+r"(result) :: "memory");
  return result;
#endif
}

static inline __attribute__((always_inline)) int16_t mlk_ntt_gs_xor(
    int16_t value, int16_t zeta, unsigned distance, unsigned tid) {
#if defined(PQC_NTTBF_K)
  (void)tid;
  switch (distance) {
  case 2: return vx_nttbf_gs_k_xor2(value, zeta);
  case 4: return vx_nttbf_gs_k_xor4(value, zeta);
  case 8: return vx_nttbf_gs_k_xor8(value, zeta);
  case 16: return vx_nttbf_gs_k_xor16(value, zeta);
  default: __builtin_unreachable();
  }
#else
  const int16_t peer = (int16_t)vx_shfl_bfly((uint16_t)value, distance, 31, 0);
  const uint32_t high = 0u - (uint32_t)((tid & distance) != 0);
  const int16_t a = mlk_select_i16(value, peer, high);
  const int16_t b = mlk_select_i16(peer, value, high);
  const int16_t sum = mlk_barrett_reduce((int16_t)(a + b));
  const int16_t product = mlk_ntt_fqmul((int16_t)(b - a), zeta);
  int16_t result = mlk_select_i16(sum, product, high);
  asm volatile ("" : "+r"(result) :: "memory");
  return result;
#endif
}

// W32 needs a 32-lane ALU datapath because SHFL cannot cross SIMD groups.
static __attribute__((noinline)) void mlk_ntt_w32(int16_t* r, unsigned tid) {
  int16_t x0 = r[tid + 32u * 0u];
  int16_t x1 = r[tid + 32u * 1u];
  int16_t x2 = r[tid + 32u * 2u];
  int16_t x3 = r[tid + 32u * 3u];
  int16_t x4 = r[tid + 32u * 4u];
  int16_t x5 = r[tid + 32u * 5u];
  int16_t x6 = r[tid + 32u * 6u];
  int16_t x7 = r[tid + 32u * 7u];

  mlk_ntt_ct_local(x0, x4, mlk_zetas[1]);
  mlk_ntt_ct_local(x1, x5, mlk_zetas[1]);
  mlk_ntt_ct_local(x2, x6, mlk_zetas[1]);
  mlk_ntt_ct_local(x3, x7, mlk_zetas[1]);
  mlk_ntt_ct_local(x0, x2, mlk_zetas[2]);
  mlk_ntt_ct_local(x1, x3, mlk_zetas[2]);
  mlk_ntt_ct_local(x4, x6, mlk_zetas[3]);
  mlk_ntt_ct_local(x5, x7, mlk_zetas[3]);
  mlk_ntt_ct_local(x0, x1, mlk_zetas[4]);
  mlk_ntt_ct_local(x2, x3, mlk_zetas[5]);
  mlk_ntt_ct_local(x4, x5, mlk_zetas[6]);
  mlk_ntt_ct_local(x6, x7, mlk_zetas[7]);

  x0 = mlk_ntt_ct_xor(x0, mlk_zetas[8], 16, tid);
  x1 = mlk_ntt_ct_xor(x1, mlk_zetas[9], 16, tid);
  x2 = mlk_ntt_ct_xor(x2, mlk_zetas[10], 16, tid);
  x3 = mlk_ntt_ct_xor(x3, mlk_zetas[11], 16, tid);
  x4 = mlk_ntt_ct_xor(x4, mlk_zetas[12], 16, tid);
  x5 = mlk_ntt_ct_xor(x5, mlk_zetas[13], 16, tid);
  x6 = mlk_ntt_ct_xor(x6, mlk_zetas[14], 16, tid);
  x7 = mlk_ntt_ct_xor(x7, mlk_zetas[15], 16, tid);

  const unsigned block8 = tid >> 4;
  x0 = mlk_ntt_ct_xor(x0, mlk_zetas[16 + block8], 8, tid);
  x1 = mlk_ntt_ct_xor(x1, mlk_zetas[18 + block8], 8, tid);
  x2 = mlk_ntt_ct_xor(x2, mlk_zetas[20 + block8], 8, tid);
  x3 = mlk_ntt_ct_xor(x3, mlk_zetas[22 + block8], 8, tid);
  x4 = mlk_ntt_ct_xor(x4, mlk_zetas[24 + block8], 8, tid);
  x5 = mlk_ntt_ct_xor(x5, mlk_zetas[26 + block8], 8, tid);
  x6 = mlk_ntt_ct_xor(x6, mlk_zetas[28 + block8], 8, tid);
  x7 = mlk_ntt_ct_xor(x7, mlk_zetas[30 + block8], 8, tid);

  const unsigned block4 = tid >> 3;
  x0 = mlk_ntt_ct_xor(x0, mlk_zetas[32 + block4], 4, tid);
  x1 = mlk_ntt_ct_xor(x1, mlk_zetas[36 + block4], 4, tid);
  x2 = mlk_ntt_ct_xor(x2, mlk_zetas[40 + block4], 4, tid);
  x3 = mlk_ntt_ct_xor(x3, mlk_zetas[44 + block4], 4, tid);
  x4 = mlk_ntt_ct_xor(x4, mlk_zetas[48 + block4], 4, tid);
  x5 = mlk_ntt_ct_xor(x5, mlk_zetas[52 + block4], 4, tid);
  x6 = mlk_ntt_ct_xor(x6, mlk_zetas[56 + block4], 4, tid);
  x7 = mlk_ntt_ct_xor(x7, mlk_zetas[60 + block4], 4, tid);

  const unsigned block2 = tid >> 2;
  x0 = mlk_ntt_ct_xor(x0, mlk_zetas[64 + block2], 2, tid);
  x1 = mlk_ntt_ct_xor(x1, mlk_zetas[72 + block2], 2, tid);
  x2 = mlk_ntt_ct_xor(x2, mlk_zetas[80 + block2], 2, tid);
  x3 = mlk_ntt_ct_xor(x3, mlk_zetas[88 + block2], 2, tid);
  x4 = mlk_ntt_ct_xor(x4, mlk_zetas[96 + block2], 2, tid);
  x5 = mlk_ntt_ct_xor(x5, mlk_zetas[104 + block2], 2, tid);
  x6 = mlk_ntt_ct_xor(x6, mlk_zetas[112 + block2], 2, tid);
  x7 = mlk_ntt_ct_xor(x7, mlk_zetas[120 + block2], 2, tid);

  r[tid + 32u * 0u] = x0;
  r[tid + 32u * 1u] = x1;
  r[tid + 32u * 2u] = x2;
  r[tid + 32u * 3u] = x3;
  r[tid + 32u * 4u] = x4;
  r[tid + 32u * 5u] = x5;
  r[tid + 32u * 6u] = x6;
  r[tid + 32u * 7u] = x7;
}

static __attribute__((noinline)) void mlk_invntt_w32(int16_t* r, unsigned tid) {
  int16_t x0 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 0u], 1441));
  int16_t x1 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 1u], 1441));
  int16_t x2 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 2u], 1441));
  int16_t x3 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 3u], 1441));
  int16_t x4 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 4u], 1441));
  int16_t x5 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 5u], 1441));
  int16_t x6 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 6u], 1441));
  int16_t x7 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[tid + 32u * 7u], 1441));

  const unsigned block2 = tid >> 2;
  x0 = mlk_ntt_gs_xor(x0, mlk_ntt_zeta(127 - block2), 2, tid);
  x1 = mlk_ntt_gs_xor(x1, mlk_ntt_zeta(119 - block2), 2, tid);
  x2 = mlk_ntt_gs_xor(x2, mlk_ntt_zeta(111 - block2), 2, tid);
  x3 = mlk_ntt_gs_xor(x3, mlk_ntt_zeta(103 - block2), 2, tid);
  x4 = mlk_ntt_gs_xor(x4, mlk_ntt_zeta(95 - block2), 2, tid);
  x5 = mlk_ntt_gs_xor(x5, mlk_ntt_zeta(87 - block2), 2, tid);
  x6 = mlk_ntt_gs_xor(x6, mlk_ntt_zeta(79 - block2), 2, tid);
  x7 = mlk_ntt_gs_xor(x7, mlk_ntt_zeta(71 - block2), 2, tid);

  const unsigned block4 = tid >> 3;
  x0 = mlk_ntt_gs_xor(x0, mlk_ntt_zeta(63 - block4), 4, tid);
  x1 = mlk_ntt_gs_xor(x1, mlk_ntt_zeta(59 - block4), 4, tid);
  x2 = mlk_ntt_gs_xor(x2, mlk_ntt_zeta(55 - block4), 4, tid);
  x3 = mlk_ntt_gs_xor(x3, mlk_ntt_zeta(51 - block4), 4, tid);
  x4 = mlk_ntt_gs_xor(x4, mlk_ntt_zeta(47 - block4), 4, tid);
  x5 = mlk_ntt_gs_xor(x5, mlk_ntt_zeta(43 - block4), 4, tid);
  x6 = mlk_ntt_gs_xor(x6, mlk_ntt_zeta(39 - block4), 4, tid);
  x7 = mlk_ntt_gs_xor(x7, mlk_ntt_zeta(35 - block4), 4, tid);

  const unsigned block8 = tid >> 4;
  x0 = mlk_ntt_gs_xor(x0, mlk_ntt_zeta(31 - block8), 8, tid);
  x1 = mlk_ntt_gs_xor(x1, mlk_ntt_zeta(29 - block8), 8, tid);
  x2 = mlk_ntt_gs_xor(x2, mlk_ntt_zeta(27 - block8), 8, tid);
  x3 = mlk_ntt_gs_xor(x3, mlk_ntt_zeta(25 - block8), 8, tid);
  x4 = mlk_ntt_gs_xor(x4, mlk_ntt_zeta(23 - block8), 8, tid);
  x5 = mlk_ntt_gs_xor(x5, mlk_ntt_zeta(21 - block8), 8, tid);
  x6 = mlk_ntt_gs_xor(x6, mlk_ntt_zeta(19 - block8), 8, tid);
  x7 = mlk_ntt_gs_xor(x7, mlk_ntt_zeta(17 - block8), 8, tid);

  x0 = mlk_ntt_gs_xor(x0, mlk_zetas[15], 16, tid);
  x1 = mlk_ntt_gs_xor(x1, mlk_zetas[14], 16, tid);
  x2 = mlk_ntt_gs_xor(x2, mlk_zetas[13], 16, tid);
  x3 = mlk_ntt_gs_xor(x3, mlk_zetas[12], 16, tid);
  x4 = mlk_ntt_gs_xor(x4, mlk_zetas[11], 16, tid);
  x5 = mlk_ntt_gs_xor(x5, mlk_zetas[10], 16, tid);
  x6 = mlk_ntt_gs_xor(x6, mlk_zetas[9], 16, tid);
  x7 = mlk_ntt_gs_xor(x7, mlk_zetas[8], 16, tid);

  mlk_ntt_gs_local(x0, x1, mlk_zetas[7]);
  mlk_ntt_gs_local(x2, x3, mlk_zetas[6]);
  mlk_ntt_gs_local(x4, x5, mlk_zetas[5]);
  mlk_ntt_gs_local(x6, x7, mlk_zetas[4]);
  mlk_ntt_gs_local(x0, x2, mlk_zetas[3]);
  mlk_ntt_gs_local(x1, x3, mlk_zetas[3]);
  mlk_ntt_gs_local(x4, x6, mlk_zetas[2]);
  mlk_ntt_gs_local(x5, x7, mlk_zetas[2]);
  mlk_ntt_gs_local(x0, x4, mlk_zetas[1]);
  mlk_ntt_gs_local(x1, x5, mlk_zetas[1]);
  mlk_ntt_gs_local(x2, x6, mlk_zetas[1]);
  mlk_ntt_gs_local(x3, x7, mlk_zetas[1]);

  r[tid + 32u * 0u] = x0;
  r[tid + 32u * 1u] = x1;
  r[tid + 32u * 2u] = x2;
  r[tid + 32u * 3u] = x3;
  r[tid + 32u * 4u] = x4;
  r[tid + 32u * 5u] = x5;
  r[tid + 32u * 6u] = x6;
  r[tid + 32u * 7u] = x7;
}
#endif

#if defined(PQC_NTT_SMEM32)
static_assert(VX_CFG_NUM_LSU_LANES == 32, "scratch NTT requires 32 LSU lanes");

// One 32-bit slot per coefficient and a padded 36-word stride avoid LMEM bank conflicts.
static __attribute__((noinline)) void mlk_ntt_smem_w32(
    int16_t* r, int32_t* scratch, unsigned tid) {
  int16_t x0 = r[tid + 32u * 0u];
  int16_t x1 = r[tid + 32u * 1u];
  int16_t x2 = r[tid + 32u * 2u];
  int16_t x3 = r[tid + 32u * 3u];
  int16_t x4 = r[tid + 32u * 4u];
  int16_t x5 = r[tid + 32u * 5u];
  int16_t x6 = r[tid + 32u * 6u];
  int16_t x7 = r[tid + 32u * 7u];

  mlk_ntt_ct_local(x0, x4, mlk_zetas[1]);
  mlk_ntt_ct_local(x1, x5, mlk_zetas[1]);
  mlk_ntt_ct_local(x2, x6, mlk_zetas[1]);
  mlk_ntt_ct_local(x3, x7, mlk_zetas[1]);
  mlk_ntt_ct_local(x0, x2, mlk_zetas[2]);
  mlk_ntt_ct_local(x1, x3, mlk_zetas[2]);
  mlk_ntt_ct_local(x4, x6, mlk_zetas[3]);
  mlk_ntt_ct_local(x5, x7, mlk_zetas[3]);
  mlk_ntt_ct_local(x0, x1, mlk_zetas[4]);
  mlk_ntt_ct_local(x2, x3, mlk_zetas[5]);
  mlk_ntt_ct_local(x4, x5, mlk_zetas[6]);
  mlk_ntt_ct_local(x6, x7, mlk_zetas[7]);

  scratch[36u * 0u + tid] = x0;
  scratch[36u * 1u + tid] = x1;
  scratch[36u * 2u + tid] = x2;
  scratch[36u * 3u + tid] = x3;
  scratch[36u * 4u + tid] = x4;
  scratch[36u * 5u + tid] = x5;
  scratch[36u * 6u + tid] = x6;
  scratch[36u * 7u + tid] = x7;
  __syncthreads();

  const unsigned group = tid >> 2;
  const unsigned column = tid & 3u;
  const unsigned first = 36u * group + column;
  x0 = (int16_t)scratch[first + 4u * 0u];
  x1 = (int16_t)scratch[first + 4u * 1u];
  x2 = (int16_t)scratch[first + 4u * 2u];
  x3 = (int16_t)scratch[first + 4u * 3u];
  x4 = (int16_t)scratch[first + 4u * 4u];
  x5 = (int16_t)scratch[first + 4u * 5u];
  x6 = (int16_t)scratch[first + 4u * 6u];
  x7 = (int16_t)scratch[first + 4u * 7u];

  const int16_t zeta16 = mlk_ntt_zeta(8u + group);
  mlk_ntt_ct_local(x0, x4, zeta16);
  mlk_ntt_ct_local(x1, x5, zeta16);
  mlk_ntt_ct_local(x2, x6, zeta16);
  mlk_ntt_ct_local(x3, x7, zeta16);
  const int16_t zeta8lo = mlk_ntt_zeta(16u + 2u * group);
  const int16_t zeta8hi = mlk_ntt_zeta(17u + 2u * group);
  mlk_ntt_ct_local(x0, x2, zeta8lo);
  mlk_ntt_ct_local(x1, x3, zeta8lo);
  mlk_ntt_ct_local(x4, x6, zeta8hi);
  mlk_ntt_ct_local(x5, x7, zeta8hi);
  mlk_ntt_ct_local(x0, x1, mlk_ntt_zeta(32u + 4u * group));
  mlk_ntt_ct_local(x2, x3, mlk_ntt_zeta(33u + 4u * group));
  mlk_ntt_ct_local(x4, x5, mlk_ntt_zeta(34u + 4u * group));
  mlk_ntt_ct_local(x6, x7, mlk_ntt_zeta(35u + 4u * group));

  const unsigned second = 36u * group + column;
  scratch[second + 0u] = x0;
  scratch[second + 4u] = x1;
  scratch[second + 9u] = x2;
  scratch[second + 13u] = x3;
  scratch[second + 18u] = x4;
  scratch[second + 22u] = x5;
  scratch[second + 27u] = x6;
  scratch[second + 31u] = x7;
  __syncthreads();

  const unsigned contiguous = 9u * tid;
  x0 = (int16_t)scratch[contiguous + 0u];
  x1 = (int16_t)scratch[contiguous + 1u];
  x2 = (int16_t)scratch[contiguous + 2u];
  x3 = (int16_t)scratch[contiguous + 3u];
  x4 = (int16_t)scratch[contiguous + 4u];
  x5 = (int16_t)scratch[contiguous + 5u];
  x6 = (int16_t)scratch[contiguous + 6u];
  x7 = (int16_t)scratch[contiguous + 7u];

  const int16_t zeta2lo = mlk_ntt_zeta(64u + 2u * tid);
  const int16_t zeta2hi = mlk_ntt_zeta(65u + 2u * tid);
  mlk_ntt_ct_local(x0, x2, zeta2lo);
  mlk_ntt_ct_local(x1, x3, zeta2lo);
  mlk_ntt_ct_local(x4, x6, zeta2hi);
  mlk_ntt_ct_local(x5, x7, zeta2hi);

  r[8u * tid + 0u] = x0;
  r[8u * tid + 1u] = x1;
  r[8u * tid + 2u] = x2;
  r[8u * tid + 3u] = x3;
  r[8u * tid + 4u] = x4;
  r[8u * tid + 5u] = x5;
  r[8u * tid + 6u] = x6;
  r[8u * tid + 7u] = x7;
}

static __attribute__((noinline)) void mlk_invntt_smem_w32(
    int16_t* r, int32_t* scratch, unsigned tid) {
  int16_t x0 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 0u], 1441));
  int16_t x1 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 1u], 1441));
  int16_t x2 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 2u], 1441));
  int16_t x3 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 3u], 1441));
  int16_t x4 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 4u], 1441));
  int16_t x5 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 5u], 1441));
  int16_t x6 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 6u], 1441));
  int16_t x7 = mlk_ntt_keep_i16(mlk_ntt_fqmul(r[8u * tid + 7u], 1441));

  const int16_t zeta2lo = mlk_ntt_zeta(127u - 2u * tid);
  const int16_t zeta2hi = mlk_ntt_zeta(126u - 2u * tid);
  mlk_ntt_gs_local(x0, x2, zeta2lo);
  mlk_ntt_gs_local(x1, x3, zeta2lo);
  mlk_ntt_gs_local(x4, x6, zeta2hi);
  mlk_ntt_gs_local(x5, x7, zeta2hi);

  const unsigned contiguous = 9u * tid;
  scratch[contiguous + 0u] = x0;
  scratch[contiguous + 1u] = x1;
  scratch[contiguous + 2u] = x2;
  scratch[contiguous + 3u] = x3;
  scratch[contiguous + 4u] = x4;
  scratch[contiguous + 5u] = x5;
  scratch[contiguous + 6u] = x6;
  scratch[contiguous + 7u] = x7;
  __syncthreads();

  const unsigned group = tid >> 2;
  const unsigned column = tid & 3u;
  const unsigned second = 36u * group + column;
  x0 = (int16_t)scratch[second + 0u];
  x1 = (int16_t)scratch[second + 4u];
  x2 = (int16_t)scratch[second + 9u];
  x3 = (int16_t)scratch[second + 13u];
  x4 = (int16_t)scratch[second + 18u];
  x5 = (int16_t)scratch[second + 22u];
  x6 = (int16_t)scratch[second + 27u];
  x7 = (int16_t)scratch[second + 31u];

  mlk_ntt_gs_local(x0, x1, mlk_ntt_zeta(63u - 4u * group));
  mlk_ntt_gs_local(x2, x3, mlk_ntt_zeta(62u - 4u * group));
  mlk_ntt_gs_local(x4, x5, mlk_ntt_zeta(61u - 4u * group));
  mlk_ntt_gs_local(x6, x7, mlk_ntt_zeta(60u - 4u * group));
  const int16_t zeta8lo = mlk_ntt_zeta(31u - 2u * group);
  const int16_t zeta8hi = mlk_ntt_zeta(30u - 2u * group);
  mlk_ntt_gs_local(x0, x2, zeta8lo);
  mlk_ntt_gs_local(x1, x3, zeta8lo);
  mlk_ntt_gs_local(x4, x6, zeta8hi);
  mlk_ntt_gs_local(x5, x7, zeta8hi);
  const int16_t zeta16 = mlk_ntt_zeta(15u - group);
  mlk_ntt_gs_local(x0, x4, zeta16);
  mlk_ntt_gs_local(x1, x5, zeta16);
  mlk_ntt_gs_local(x2, x6, zeta16);
  mlk_ntt_gs_local(x3, x7, zeta16);

  const unsigned first = 36u * group + column;
  scratch[first + 4u * 0u] = x0;
  scratch[first + 4u * 1u] = x1;
  scratch[first + 4u * 2u] = x2;
  scratch[first + 4u * 3u] = x3;
  scratch[first + 4u * 4u] = x4;
  scratch[first + 4u * 5u] = x5;
  scratch[first + 4u * 6u] = x6;
  scratch[first + 4u * 7u] = x7;
  __syncthreads();

  x0 = (int16_t)scratch[36u * 0u + tid];
  x1 = (int16_t)scratch[36u * 1u + tid];
  x2 = (int16_t)scratch[36u * 2u + tid];
  x3 = (int16_t)scratch[36u * 3u + tid];
  x4 = (int16_t)scratch[36u * 4u + tid];
  x5 = (int16_t)scratch[36u * 5u + tid];
  x6 = (int16_t)scratch[36u * 6u + tid];
  x7 = (int16_t)scratch[36u * 7u + tid];

  mlk_ntt_gs_local(x0, x1, mlk_zetas[7]);
  mlk_ntt_gs_local(x2, x3, mlk_zetas[6]);
  mlk_ntt_gs_local(x4, x5, mlk_zetas[5]);
  mlk_ntt_gs_local(x6, x7, mlk_zetas[4]);
  mlk_ntt_gs_local(x0, x2, mlk_zetas[3]);
  mlk_ntt_gs_local(x1, x3, mlk_zetas[3]);
  mlk_ntt_gs_local(x4, x6, mlk_zetas[2]);
  mlk_ntt_gs_local(x5, x7, mlk_zetas[2]);
  mlk_ntt_gs_local(x0, x4, mlk_zetas[1]);
  mlk_ntt_gs_local(x1, x5, mlk_zetas[1]);
  mlk_ntt_gs_local(x2, x6, mlk_zetas[1]);
  mlk_ntt_gs_local(x3, x7, mlk_zetas[1]);

  r[tid + 32u * 0u] = x0;
  r[tid + 32u * 1u] = x1;
  r[tid + 32u * 2u] = x2;
  r[tid + 32u * 3u] = x3;
  r[tid + 32u * 4u] = x4;
  r[tid + 32u * 5u] = x5;
  r[tid + 32u * 6u] = x6;
  r[tid + 32u * 7u] = x7;
}
#endif
#endif

#endif
