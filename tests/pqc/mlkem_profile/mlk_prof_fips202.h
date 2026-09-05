// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Counting FIPS202 backend.
//
// Every hook records the call and returns MLK_NATIVE_FUNC_FALLBACK, which the
// library's contract defines as "I did nothing, run your own" -- so the code
// under measurement is byte-for-byte the code the baseline runs. That is the
// whole point: a profile taken from a different build measures that build.

#ifndef MLK_PROF_FIPS202_H
#define MLK_PROF_FIPS202_H

#if !defined(__ASSEMBLER__)
// The upstream backends sit inside the library tree, where "../api.h"
// resolves. This one lives with the test, so it names the header from the
// include path instead.
#include "src/fips202/native/api.h"
#include "mlk_prof_counters.h"

#define MLK_USE_NATIVE_FIPS202_X1
static MLK_INLINE int mlk_keccak_f1600_x1_native(uint64_t *state)
{
  (void)state;
  mlk_prof_counts[MLK_PROF_KECCAK_X1]++;
#if defined(PQC_ABLATE_KECCAK)
  /* Claim success without permuting: the call structure is untouched and the
   * permutation work vanishes, so the cycle delta against the baseline is
   * Keccak's true cost. Results are wrong by construction -- this build is an
   * ablation, never a correctness test. Ablating only x1 and letting x4 fall
   * back as usual keeps the call counts identical to the baseline's. */
  return MLK_NATIVE_FUNC_SUCCESS;
#else
  return MLK_NATIVE_FUNC_FALLBACK;
#endif
}

#define MLK_USE_NATIVE_FIPS202_X4
static MLK_INLINE int mlk_keccak_f1600_x4_native(uint64_t *state)
{
  (void)state;
  mlk_prof_counts[MLK_PROF_KECCAK_X4]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

#endif /* !__ASSEMBLER__ */
#endif /* MLK_PROF_FIPS202_H */
