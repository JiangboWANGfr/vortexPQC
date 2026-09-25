// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Baseline configuration plus the two counting backends.
//
// The baseline config is included rather than restated: this build has to be
// the mlkem test's build in every respect except the counting, or the counts
// belong to something else.

#ifndef VORTEX_PROF_CONFIG_H
#define VORTEX_PROF_CONFIG_H

#include "vortex_mlkem_config.h"

#if defined(PQC_ZEROIZE_WARP) && defined(__VORTEX__) && !defined(__ASSEMBLER__)
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <VX_config.h>
#include <vx_intrinsics.h>
#define MLK_CONFIG_CUSTOM_ZEROIZE
extern uint32_t mlk_zeroize_parallel[VX_CFG_NUM_WARPS];
void mlk_profile_zeroize_warp(void* ptr, size_t len);
static inline void mlk_zeroize(void* ptr, size_t len) {
  if (mlk_zeroize_parallel[vx_warp_id()]) {
    mlk_profile_zeroize_warp(ptr, len);
  } else {
    memset(ptr, 0, len);
    asm volatile ("" : : "r"(ptr) : "memory");
  }
}
#endif

#if defined(PQC_SERIAL_FIPS202_ONLY) || defined(PQC_KECCAK_SG25)
#define MLK_CONFIG_SERIAL_FIPS202_ONLY
#endif

// The profile build owns the FIPS-202 backend, because the counters have to be
// in it. KECCAK=pe does NOT select a different backend here the way it does in
// the plain mlkem build -- it routes to the instruction from inside
// mlk_prof_fips202.h, so the count and the instruction stay in one hook. The
// included baseline config has already set these for its own KECCAK arms, so
// undefine before redefining rather than leaving a redefinition warning.
#undef MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202
#undef MLK_CONFIG_FIPS202_BACKEND_FILE
#undef MLK_CONFIG_FIPS202_CUSTOM_HEADER
#if defined(PQC_KECCAK_SG25)
#define MLK_CONFIG_FIPS202_CUSTOM_HEADER "profile_sg25_fips202.h"
#else
#define MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLK_CONFIG_FIPS202_BACKEND_FILE "mlk_prof_fips202.h"
#endif

#define MLK_CONFIG_USE_NATIVE_BACKEND_ARITH
#define MLK_CONFIG_ARITH_BACKEND_FILE "mlk_prof_arith.h"

#endif // VORTEX_PROF_CONFIG_H
