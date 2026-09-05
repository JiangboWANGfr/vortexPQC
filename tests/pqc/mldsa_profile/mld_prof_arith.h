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

#define MLD_USE_NATIVE_NTT
static MLD_INLINE int mld_ntt_native(int32_t p[MLDSA_N])
{
  (void)p;
  mld_prof_counts[MLD_PROF_NTT]++;
#if defined(PQC_ABLATE_NTT)
  return MLD_NATIVE_FUNC_SUCCESS;
#else
  return MLD_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLD_USE_NATIVE_INTT
static MLD_INLINE int mld_intt_native(int32_t p[MLDSA_N])
{
  (void)p;
  mld_prof_counts[MLD_PROF_INTT]++;
#if defined(PQC_ABLATE_NTT)
  return MLD_NATIVE_FUNC_SUCCESS;
#else
  return MLD_NATIVE_FUNC_FALLBACK;
#endif
}

#endif /* !__ASSEMBLER__ */
#endif /* MLD_PROF_ARITH_H */
