// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// mldsa-native configuration for the Vortex baseline. Mirrors the ML-KEM one
// where the two libraries agree, and adds what ML-DSA needs and ML-KEM did not.

#ifndef VORTEX_MLDSA_CONFIG_H
#define VORTEX_MLDSA_CONFIG_H

// ML-DSA-65: the FIPS 204 parameter set at NIST level 3, matching ML-KEM-768
// so the two baselines sit at the same security level and their costs compare.
#define MLD_CONFIG_PARAMETER_SET 65

#define MLD_CONFIG_NAMESPACE_PREFIX mldsa

// Same reasoning as ML-KEM: the derandomized entry points take explicit
// randomness, so no randombytes is needed and two runs of a build are
// byte-identical.
#define MLD_CONFIG_NO_RANDOMIZED_API

// Union the largest allocations. Not enough on its own -- see the allocator
// below -- but it lowers the peak the arena has to cover.
#define MLD_CONFIG_REDUCE_RAM

// Stack allocation is not viable here: one mld_polymat is 30 KB against an
// 8 KB per-thread stack. See mld_vortex_alloc.h.
#define MLD_CONFIG_CUSTOM_ALLOC_FREE
#if !defined(__ASSEMBLER__)
#include "mld_vortex_alloc.h"
#define MLD_CUSTOM_ALLOC(v, T, N) \
  T *v = (T *)mld_arena_alloc((uint32_t)(sizeof(T) * (N)))
#define MLD_CUSTOM_FREE(v, T, N) \
  do { (void)(v); } while (0)
#endif

#endif // VORTEX_MLDSA_CONFIG_H
