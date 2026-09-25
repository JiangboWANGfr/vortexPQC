// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Counting arithmetic backend with optional cooperative paths and cycle probes.

#ifndef MLK_PROF_ARITH_H
#define MLK_PROF_ARITH_H

#if !defined(__ASSEMBLER__)
// The upstream backends sit inside the library tree, where "../api.h"
// resolves. This one lives with the test, so it names the header from the
// include path instead.
#include "src/native/api.h"
#include "mlk_prof_counters.h"

#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_COOP)
void mlk_profile_reduce(int16_t* p);
void mlk_profile_mulcache(int16_t* x, const int16_t* a);
void mlk_profile_basemul(int16_t* r, const int16_t* a,
                         const int16_t* b, const int16_t* cache);
#endif

#if defined(PQC_NTT_COOP)
void mlk_profile_ntt(int16_t* p, unsigned inverse);
#endif

#if defined(PQC_REJ_WARP)
int mlk_profile_rej_uniform(int16_t* r, unsigned len,
                            const uint8_t* buf, unsigned buflen);
#endif

#if defined(PQC_CODEC_WARP)
void mlk_profile_tobytes(uint8_t* r, const int16_t* a);
void mlk_profile_frombytes(int16_t* a, const uint8_t* r);
#if defined(PQC_CODEC_COMPRESS_WARP)
void mlk_profile_compress(uint8_t* r, const int16_t* a, unsigned bits);
void mlk_profile_decompress(int16_t* r, const uint8_t* a, unsigned bits);
#endif
#endif

#define MLK_USE_NATIVE_NTT
static MLK_INLINE int mlk_ntt_native(int16_t p[MLKEM_N])
{
  (void)p;
  mlk_prof_counts[vx_hart_id()][MLK_PROF_NTT]++;
#if defined(PQC_NTT_COOP)
  mlk_profile_ntt(p, 0);
  return MLK_NATIVE_FUNC_SUCCESS;
#elif defined(PQC_ABLATE_NTT)
  /* See the Keccak hook: skipping the transform leaves the input bound (< q)
   * in place, which still satisfies the wider bound the contract promises on
   * success, so the library carries on rather than tripping an assertion. */
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLK_USE_NATIVE_INTT
static MLK_INLINE int mlk_intt_native(int16_t p[MLKEM_N])
{
  (void)p;
  mlk_prof_counts[vx_hart_id()][MLK_PROF_INTT]++;
#if defined(PQC_NTT_COOP)
  mlk_profile_ntt(p, 1);
  return MLK_NATIVE_FUNC_SUCCESS;
#elif defined(PQC_ABLATE_NTT)
  /* See the Keccak hook: skipping the transform leaves the input bound (< q)
   * in place, which still satisfies the wider bound the contract promises on
   * success, so the library carries on rather than tripping an assertion. */
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLK_USE_NATIVE_POLY_REDUCE
static MLK_INLINE int mlk_poly_reduce_native(int16_t p[MLKEM_N])
{
  (void)p;
  mlk_prof_counts[vx_hart_id()][MLK_PROF_POLY_REDUCE]++;
#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_REDUCE)
  mlk_profile_reduce(p);
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLK_USE_NATIVE_POLY_MULCACHE_COMPUTE
static MLK_INLINE int mlk_poly_mulcache_compute_native(
    int16_t x[MLKEM_N / 2], const int16_t a[MLKEM_N])
{
  (void)x; (void)a;
  mlk_prof_counts[vx_hart_id()][MLK_PROF_MULCACHE]++;
#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_MULCACHE)
  mlk_profile_mulcache(x, a);
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLK_USE_NATIVE_POLYVEC_BASEMUL_ACC_MONTGOMERY_CACHED
#if MLK_CONFIG_PARAMETER_SET == 512
#define MLK_PROFILE_BASEMUL_NATIVE mlk_polyvec_basemul_acc_montgomery_cached_k2_native
#elif MLK_CONFIG_PARAMETER_SET == 768
#define MLK_PROFILE_BASEMUL_NATIVE mlk_polyvec_basemul_acc_montgomery_cached_k3_native
#else
#define MLK_PROFILE_BASEMUL_NATIVE mlk_polyvec_basemul_acc_montgomery_cached_k4_native
#endif
static MLK_INLINE int MLK_PROFILE_BASEMUL_NATIVE(
    int16_t r[MLKEM_N], const int16_t a[MLKEM_K * MLKEM_N],
    const int16_t b[MLKEM_K * MLKEM_N],
    const int16_t b_cache[MLKEM_K * (MLKEM_N / 2)])
{
  (void)r; (void)a; (void)b; (void)b_cache;
  mlk_prof_counts[vx_hart_id()][MLK_PROF_BASEMUL]++;
#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_BASEMUL)
  mlk_profile_basemul(r, a, b, b_cache);
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#undef MLK_PROFILE_BASEMUL_NATIVE

#define MLK_USE_NATIVE_REJ_UNIFORM
static MLK_INLINE int mlk_rej_uniform_native(int16_t *r, unsigned len,
                                             const uint8_t *buf, unsigned buflen)
{
  mlk_prof_counts[vx_hart_id()][MLK_PROF_REJ_UNIFORM]++;
#if defined(PQC_REJ_WARP)
  return mlk_profile_rej_uniform(r, len, buf, buflen);
#else
  (void)r; (void)len; (void)buf; (void)buflen;
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#if defined(PQC_CODEC_WARP)
#define MLK_USE_NATIVE_POLY_TOBYTES
static MLK_INLINE int mlk_poly_tobytes_native(
    uint8_t r[MLKEM_POLYBYTES], const int16_t a[MLKEM_N])
{
  mlk_profile_tobytes(r, a);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_FROMBYTES
static MLK_INLINE int mlk_poly_frombytes_native(
    int16_t a[MLKEM_N], const uint8_t r[MLKEM_POLYBYTES])
{
  mlk_profile_frombytes(a, r);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#if defined(PQC_CODEC_COMPRESS_WARP)
#if MLK_CONFIG_PARAMETER_SET == 1024
#define MLK_USE_NATIVE_POLY_COMPRESS_D5
static MLK_INLINE int mlk_poly_compress_d5_native(
    uint8_t r[MLKEM_POLYCOMPRESSEDBYTES_D5], const int16_t a[MLKEM_N])
{
  mlk_profile_compress(r, a, 5);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_DECOMPRESS_D5
static MLK_INLINE int mlk_poly_decompress_d5_native(
    int16_t r[MLKEM_N], const uint8_t a[MLKEM_POLYCOMPRESSEDBYTES_D5])
{
  mlk_profile_decompress(r, a, 5);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_COMPRESS_D11
static MLK_INLINE int mlk_poly_compress_d11_native(
    uint8_t r[MLKEM_POLYCOMPRESSEDBYTES_D11], const int16_t a[MLKEM_N])
{
  mlk_profile_compress(r, a, 11);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_DECOMPRESS_D11
static MLK_INLINE int mlk_poly_decompress_d11_native(
    int16_t r[MLKEM_N], const uint8_t a[MLKEM_POLYCOMPRESSEDBYTES_D11])
{
  mlk_profile_decompress(r, a, 11);
  return MLK_NATIVE_FUNC_SUCCESS;
}
#else
#define MLK_USE_NATIVE_POLY_COMPRESS_D4
static MLK_INLINE int mlk_poly_compress_d4_native(
    uint8_t r[MLKEM_POLYCOMPRESSEDBYTES_D4], const int16_t a[MLKEM_N])
{
  mlk_profile_compress(r, a, 4);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_DECOMPRESS_D4
static MLK_INLINE int mlk_poly_decompress_d4_native(
    int16_t r[MLKEM_N], const uint8_t a[MLKEM_POLYCOMPRESSEDBYTES_D4])
{
  mlk_profile_decompress(r, a, 4);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_COMPRESS_D10
static MLK_INLINE int mlk_poly_compress_d10_native(
    uint8_t r[MLKEM_POLYCOMPRESSEDBYTES_D10], const int16_t a[MLKEM_N])
{
  mlk_profile_compress(r, a, 10);
  return MLK_NATIVE_FUNC_SUCCESS;
}

#define MLK_USE_NATIVE_POLY_DECOMPRESS_D10
static MLK_INLINE int mlk_poly_decompress_d10_native(
    int16_t r[MLKEM_N], const uint8_t a[MLKEM_POLYCOMPRESSEDBYTES_D10])
{
  mlk_profile_decompress(r, a, 10);
  return MLK_NATIVE_FUNC_SUCCESS;
}
#endif
#endif
#endif

#endif /* !__ASSEMBLER__ */
#endif /* MLK_PROF_ARITH_H */
