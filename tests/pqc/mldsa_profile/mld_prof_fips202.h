// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Counting FIPS202 backend for ML-DSA. Each hook records the call and returns
// MLD_NATIVE_FUNC_FALLBACK, so the library runs its own C code and the profile
// describes the baseline rather than a build that resembles it.
//
// PQC_ABLATE_KECCAK makes the x1 hook claim success without permuting: the call
// structure is unchanged and the permutation work vanishes, so the cycle delta
// is Keccak's measured cost. Results are wrong by construction. Only x1 is
// ablated, so x4 still falls back to four x1 calls and the counts stay
// comparable with the baseline's.

#ifndef MLD_PROF_FIPS202_H
#define MLD_PROF_FIPS202_H

#if !defined(__ASSEMBLER__)
#include "src/fips202/native/api.h"
#include "mld_prof_counters.h"

#define MLD_USE_NATIVE_FIPS202_X1
static MLD_INLINE int mld_keccak_f1600_x1_native(uint64_t *state)
{
  (void)state;
  mld_prof_counts[MLD_PROF_KECCAK_X1]++;
#if defined(PQC_ABLATE_KECCAK)
  return MLD_NATIVE_FUNC_SUCCESS;
#else
  return MLD_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLD_USE_NATIVE_FIPS202_X4
static MLD_INLINE int mld_keccak_f1600_x4_native(uint64_t *state)
{
  (void)state;
  mld_prof_counts[MLD_PROF_KECCAK_X4]++;
  return MLD_NATIVE_FUNC_FALLBACK;
}

#endif /* !__ASSEMBLER__ */
#endif /* MLD_PROF_FIPS202_H */
