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
#if defined(PQC_REJ_WARP)
int mld_profile_rej_uniform(int32_t* r, unsigned len,
                            const uint8_t* buf, unsigned buflen);
#endif
#if defined(PQC_REJ_ETA_WARP)
int mld_profile_rej_eta(int32_t* r, unsigned len, const uint8_t* buf,
                        unsigned buflen, unsigned eta);
#endif
#if defined(PQC_POINTWISE_ISE)
void mld_profile_pointwise(int32_t* a, const int32_t* b);
#endif

#if defined(PQC_REJ_ETA_WARP)
#if MLD_CONFIG_PARAMETER_SET == 65
#define MLD_USE_NATIVE_REJ_UNIFORM_ETA4
static MLD_INLINE int mld_rej_uniform_eta4_native(int32_t* r, unsigned len,
                                                  const uint8_t* buf,
                                                  unsigned buflen)
{
  return mld_profile_rej_eta(r, len, buf, buflen, 4);
}
#else
#define MLD_USE_NATIVE_REJ_UNIFORM_ETA2
static MLD_INLINE int mld_rej_uniform_eta2_native(int32_t* r, unsigned len,
                                                  const uint8_t* buf,
                                                  unsigned buflen)
{
  return mld_profile_rej_eta(r, len, buf, buflen, 2);
}
#endif
#endif
#if defined(PQC_POINTWISE_L5_W32)
void mld_profile_pointwise_acc(int32_t* w, const int32_t u[MLDSA_L][MLDSA_N],
                              const int32_t v[MLDSA_L][MLDSA_N]);
#endif
#if defined(PQC_SIGN_DECOMPOSE_WARP)
void mld_profile_decompose(int32_t* a1, int32_t* a0);
#endif
#if defined(PQC_SIGN_HINT_WARP)
void mld_profile_use_hint(int32_t* a, const int32_t* h);
#endif
#if defined(PQC_SIGN_CHKNORM_WARP)
int mld_profile_chknorm(const int32_t* a, int32_t bound);
#endif
#if defined(PQC_SIGN_CADDQ_WARP)
void mld_profile_caddq(int32_t* a);
#endif
#if defined(PQC_SIGN_ZUNPACK_WARP)
void mld_profile_zunpack(int32_t* r, const uint8_t* a);
#endif

#if defined(PQC_REJ_WARP)
#define MLD_USE_NATIVE_REJ_UNIFORM
static MLD_INLINE int mld_rej_uniform_native(int32_t* r, unsigned len,
                                            const uint8_t* buf, unsigned buflen)
{
  return mld_profile_rej_uniform(r, len, buf, buflen);
}
#endif

#if defined(PQC_SIGN_DECOMPOSE_WARP)
#if MLD_CONFIG_PARAMETER_SET == 44
#define MLD_USE_NATIVE_POLY_DECOMPOSE_88
static MLD_INLINE int mld_poly_decompose_88_native(int32_t* a1, int32_t* a0)
{
  mld_profile_decompose(a1, a0);
  return MLD_NATIVE_FUNC_SUCCESS;
}
#else
#define MLD_USE_NATIVE_POLY_DECOMPOSE_32
static MLD_INLINE int mld_poly_decompose_32_native(int32_t* a1, int32_t* a0)
{
  mld_profile_decompose(a1, a0);
  return MLD_NATIVE_FUNC_SUCCESS;
}
#endif
#endif

#if defined(PQC_SIGN_HINT_WARP)
#if MLD_CONFIG_PARAMETER_SET == 44
#define MLD_USE_NATIVE_POLY_USE_HINT_88
static MLD_INLINE int mld_poly_use_hint_88_native(int32_t* a,
                                                  const int32_t* h)
{
  mld_profile_use_hint(a, h);
  return MLD_NATIVE_FUNC_SUCCESS;
}
#else
#define MLD_USE_NATIVE_POLY_USE_HINT_32
static MLD_INLINE int mld_poly_use_hint_32_native(int32_t* a,
                                                  const int32_t* h)
{
  mld_profile_use_hint(a, h);
  return MLD_NATIVE_FUNC_SUCCESS;
}
#endif
#endif

#if defined(PQC_SIGN_CHKNORM_WARP)
#define MLD_USE_NATIVE_POLY_CHKNORM
static MLD_INLINE int mld_poly_chknorm_native(const int32_t* a, int32_t bound)
{
  return mld_profile_chknorm(a, bound);
}
#endif

#if defined(PQC_SIGN_CADDQ_WARP)
#define MLD_USE_NATIVE_POLY_CADDQ
static MLD_INLINE int mld_poly_caddq_native(int32_t* a)
{
  mld_profile_caddq(a);
  return MLD_NATIVE_FUNC_SUCCESS;
}
#endif

#if defined(PQC_SIGN_ZUNPACK_WARP)
#if MLD_CONFIG_PARAMETER_SET == 44
#define MLD_USE_NATIVE_POLYZ_UNPACK_17
static MLD_INLINE int mld_polyz_unpack_17_native(int32_t* r,
                                                 const uint8_t* a)
#else
#define MLD_USE_NATIVE_POLYZ_UNPACK_19
static MLD_INLINE int mld_polyz_unpack_19_native(int32_t* r,
                                                 const uint8_t* a)
#endif
{
  mld_profile_zunpack(r, a);
  return MLD_NATIVE_FUNC_SUCCESS;
}
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
#if defined(PQC_PROFILE_PHASES)
  const unsigned slot = mld_prof_slot();
  const uint64_t start = vx_rdcycle();
#endif
  mld_profile_ntt(p, 0);
#if defined(PQC_PROFILE_PHASES)
  mld_prof_detail_cycles[slot][mld_prof_phase[slot]][MLD_PHASE_NTT] +=
      vx_rdcycle() - start;
#endif
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
#if defined(PQC_PROFILE_PHASES)
  const unsigned slot = mld_prof_slot();
  const uint64_t start = vx_rdcycle();
#endif
  mld_profile_ntt(p, 1);
#if defined(PQC_PROFILE_PHASES)
  mld_prof_detail_cycles[slot][mld_prof_phase[slot]][MLD_PHASE_INTT] +=
      vx_rdcycle() - start;
#endif
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

#if MLD_CONFIG_PARAMETER_SET == 44
#define MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L4
#define MLD_PROFILE_ACC_NATIVE mld_polyvecl_pointwise_acc_montgomery_l4_native
#elif MLD_CONFIG_PARAMETER_SET == 65
#define MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L5
#define MLD_PROFILE_ACC_NATIVE mld_polyvecl_pointwise_acc_montgomery_l5_native
#else
#define MLD_USE_NATIVE_POLYVECL_POINTWISE_ACC_MONTGOMERY_L7
#define MLD_PROFILE_ACC_NATIVE mld_polyvecl_pointwise_acc_montgomery_l7_native
#endif
static MLD_INLINE int MLD_PROFILE_ACC_NATIVE(
    int32_t w[MLDSA_N], const int32_t u[MLDSA_L][MLDSA_N],
    const int32_t v[MLDSA_L][MLDSA_N])
{
  const unsigned slot = mld_prof_slot();
  mld_prof_counts[slot][MLD_PROF_POINTWISE_L5]++;
  const uint64_t start = vx_rdcycle();
#if defined(PQC_POINTWISE_L5_W32)
  mld_profile_pointwise_acc(w, u, v);
#else
  for (unsigned i = 0; i < MLDSA_N; ++i) {
    int64_t sum = 0;
    for (unsigned j = 0; j < MLDSA_L; ++j)
      sum += (int64_t)u[j][i] * v[j][i];
    w[i] = mld_prof_montgomery_reduce(sum);
  }
#endif
  mld_prof_pointwise_cycles[slot][mld_prof_phase[slot]][1] += vx_rdcycle() - start;
  return MLD_NATIVE_FUNC_SUCCESS;
}

#undef MLD_PROFILE_ACC_NATIVE

#endif /* !__ASSEMBLER__ */
#endif /* MLD_PROF_ARITH_H */
