// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// The ML-DSA baseline configuration plus the two counting backends. The
// baseline is included rather than restated, so this build differs from the
// measured one only by the counting.

#ifndef VORTEX_MLDSA_PROF_CONFIG_H
#define VORTEX_MLDSA_PROF_CONFIG_H

#include "vortex_mldsa_config.h"

#define MLD_PROFILE_L (MLD_CONFIG_PARAMETER_SET == 44 ? 4 : \
                       MLD_CONFIG_PARAMETER_SET == 65 ? 5 : 7)

#if defined(PQC_ZEROIZE_WARP) && defined(__VORTEX__) && \
    !defined(__ASSEMBLER__)
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <VX_config.h>
#include <vx_intrinsics.h>
#include "mld_prof_counters.h"
#define MLD_CONFIG_CUSTOM_ZEROIZE
extern uint32_t mld_zeroize_parallel[MLD_PROF_SLOTS];
void mld_profile_zeroize_warp(void* ptr, size_t len);
static inline void mld_zeroize(void* ptr, size_t len) {
  if (mld_zeroize_parallel[mld_prof_slot()]) {
    mld_profile_zeroize_warp(ptr, len);
  } else {
    memset(ptr, 0, len);
    asm volatile ("" : : "r"(ptr) : "memory");
  }
}
#elif defined(PQC_PROFILE_PHASES) && defined(__VORTEX__) && \
    !defined(__ASSEMBLER__)
#include <stddef.h>
#include <string.h>
#include "mld_prof_counters.h"
#define MLD_CONFIG_CUSTOM_ZEROIZE
static inline void mld_zeroize(void* ptr, size_t len) {
  const unsigned slot = mld_prof_slot();
  const uint64_t start = vx_rdcycle();
  memset(ptr, 0, len);
  asm volatile ("" : : "r"(ptr) : "memory");
  mld_prof_detail_cycles[slot][mld_prof_phase[slot]][MLD_PHASE_ZEROIZE] +=
      vx_rdcycle() - start;
}
#endif

#define MLD_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLD_CONFIG_FIPS202_BACKEND_FILE "mld_prof_fips202.h"

#define MLD_CONFIG_USE_NATIVE_BACKEND_ARITH
#define MLD_CONFIG_ARITH_BACKEND_FILE "mld_prof_arith.h"

#endif // VORTEX_MLDSA_PROF_CONFIG_H
