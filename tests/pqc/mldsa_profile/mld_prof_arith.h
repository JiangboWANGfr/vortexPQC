// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Counting arithmetic backend for ML-DSA. See mld_prof_fips202.h for the
// contract; PQC_ABLATE_NTT is the transform's equivalent switch.

#ifndef MLD_PROF_ARITH_H
#define MLD_PROF_ARITH_H

#if !defined(__ASSEMBLER__)
#include "src/native/api.h"
#include "mld_prof_counters.h"
#if defined(PQC_NTT_REG32)
void mld_profile_ntt(int32_t* p, unsigned inverse);
#endif
#if defined(PQC_POINTWISE_ISE)
void mld_profile_pointwise(int32_t* a, const int32_t* b);
#endif
#if defined(PQC_POINTWISE_L5_W32)
void mld_profile_pointwise_l5(int32_t* w, const int32_t u[5][MLDSA_N],
                              const int32_t v[5][MLDSA_N]);
#endif

// The native backend is included before the library's reduce.h.
static MLD_INLINE int32_t mld_prof_montgomery_reduce(int64_t a)
{
  const int32_t t = (int32_t)((uint32_t)a * 58728449u);
  return (int32_t)((a - (int64_t)t * MLDSA_Q) >> 32);
}

#define MLD_USE_NATIVE_NTT
static MLD_INLINE int mld_ntt_native(int32_t p[MLDSA_N])
{
  (void)p;
  mld_prof_counts[mld_prof_slot()][MLD_PROF_NTT]++;
#if defined(PQC_NTT_REG32)
  mld_profile_ntt(p, 0);
  return MLD_NATIVE_FUNC_SUCCESS;
#elif defined(PQC_ABLATE_NTT)
  return MLD_NATIVE_FUNC_SUCCESS;
#else
  return MLD_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLD_USE_NATIVE_INTT
static MLD_INLINE int mld_intt_native(int32_t p[MLDSA_N])
{
  (void)p;
  mld_prof_counts[mld_prof_slot()][MLD_PROF_INTT]++;
#if defined(PQC_NTT_REG32)
  mld_profile_ntt(p, 1);
  return MLD_NATIVE_FUNC_SUCCESS;
#elif defined(PQC_ABLATE_NTT)
  return MLD_NATIVE_FUNC_SUCCESS;
#else
  return MLD_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLD_USE_NATIVE_POINTWISE_MONTGOMERY
static MLD_INLINE int mld_poly_pointwise_montgomery_native(
    int32_t a[MLDSA_N], const int32_t b[MLDSA_N])
{
  const unsigned slot = mld_prof_slot();
  mld_prof_counts[slot][MLD_PROF_POINTWISE]++;
  const uint64_t start = vx_rdcycle();
#if defined(PQC_POINTWISE_ISE)
  mld_profile_pointwise(a, b);
#else
  for (unsigned i = 0; i < MLDSA_N; ++i)
    a[i] = mld_prof_montgomery_reduce((int64_t)a[i] * b[i]);
#endif
  mld_prof_pointwise_cycles[slot][mld_prof_phase[slot]][0] += vx_rdcycle() - start;
  return MLD_NATIVE_FUNC_SUCCESS;
}

#define MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L5
static MLD_INLINE int mld_polyvecl_pointwise_acc_montgomery_l5_native(
    int32_t w[MLDSA_N], const int32_t u[5][MLDSA_N],
    const int32_t v[5][MLDSA_N])
{
  const unsigned slot = mld_prof_slot();
  mld_prof_counts[slot][MLD_PROF_POINTWISE_L5]++;
  const uint64_t start = vx_rdcycle();
#if defined(PQC_POINTWISE_L5_W32)
  mld_profile_pointwise_l5(w, u, v);
#else
  for (unsigned i = 0; i < MLDSA_N; ++i) {
    int64_t sum = 0;
    for (unsigned j = 0; j < 5; ++j)
      sum += (int64_t)u[j][i] * v[j][i];
    w[i] = mld_prof_montgomery_reduce(sum);
  }
#endif
  mld_prof_pointwise_cycles[slot][mld_prof_phase[slot]][1] += vx_rdcycle() - start;
  return MLD_NATIVE_FUNC_SUCCESS;
}

#endif /* !__ASSEMBLER__ */
#endif /* MLD_PROF_ARITH_H */
