// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Counting arithmetic backend. Same contract as the FIPS202 one: record and
// return MLK_NATIVE_FUNC_FALLBACK, so the library runs its own C code.
//
// Only the hooks a PQC unit would plausibly take over are counted. The
// compress/decompress and tobytes/frombytes hooks are left unset; they are
// byte-shuffling, not the arithmetic this project is sizing.

#ifndef MLK_PROF_ARITH_H
#define MLK_PROF_ARITH_H

#if !defined(__ASSEMBLER__)
// The upstream backends sit inside the library tree, where "../api.h"
// resolves. This one lives with the test, so it names the header from the
// include path instead.
#include "src/native/api.h"
#include "mlk_prof_counters.h"

#define MLK_USE_NATIVE_NTT
static MLK_INLINE int mlk_ntt_native(int16_t p[MLKEM_N])
{
  (void)p;
  mlk_prof_counts[MLK_PROF_NTT]++;
#if defined(PQC_ABLATE_NTT)
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
  mlk_prof_counts[MLK_PROF_INTT]++;
#if defined(PQC_ABLATE_NTT)
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
  mlk_prof_counts[MLK_PROF_POLY_REDUCE]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

#define MLK_USE_NATIVE_POLY_MULCACHE_COMPUTE
static MLK_INLINE int mlk_poly_mulcache_compute_native(
    int16_t x[MLKEM_N / 2], const int16_t a[MLKEM_N])
{
  (void)x; (void)a;
  mlk_prof_counts[MLK_PROF_MULCACHE]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

// Only the k=3 variant is defined: MLK_CONFIG_PARAMETER_SET is 768, and the
// library declares the k2/k4 hooks only for their own parameter sets.
#define MLK_USE_NATIVE_POLYVEC_BASEMUL_ACC_MONTGOMERY_CACHED
static MLK_INLINE int mlk_polyvec_basemul_acc_montgomery_cached_k3_native(
    int16_t r[MLKEM_N], const int16_t a[3 * MLKEM_N],
    const int16_t b[3 * MLKEM_N], const int16_t b_cache[3 * (MLKEM_N / 2)])
{
  (void)r; (void)a; (void)b; (void)b_cache;
  mlk_prof_counts[MLK_PROF_BASEMUL]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

#define MLK_USE_NATIVE_REJ_UNIFORM
static MLK_INLINE int mlk_rej_uniform_native(int16_t *r, unsigned len,
                                             const uint8_t *buf, unsigned buflen)
{
  (void)r; (void)len; (void)buf; (void)buflen;
  mlk_prof_counts[MLK_PROF_REJ_UNIFORM]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

#endif /* !__ASSEMBLER__ */
#endif /* MLK_PROF_ARITH_H */
