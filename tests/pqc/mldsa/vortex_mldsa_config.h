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
// MLDSA_RAM=low (default) keeps the library's low-memory path: the matrix A is
// expanded lazily, one entry at a time, instead of being materialised. That is
// what let ML-DSA-65 fit here at all before the stack was fixed -- and it costs
// twice over, because a lazily expanded A is re-derived every time it is used
// and because the lazy path has no 4-wide window, so almost none of its Keccak
// is batchable. MLDSA_RAM=full trades memory back for both.
//
// This is a software/memory Pareto choice, not a property of the algorithm, and
// the difference belongs to the software baseline. Charging it to an ISA
// extension would be crediting the extension for work a compile flag does.
#if !defined(PQC_MLDSA_RAM_FULL)
#define MLD_CONFIG_REDUCE_RAM
#endif

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

// SIMT_KECCAK=1 spreads the library's x4 Keccak batch across the lanes of a
// warp, which is what turns -t into a real lane-width axis. Off by default so
// the reference measurements stay on the pristine library.
//
// Note this axis barely exists under MLDSA_RAM=low: the lazy matrix path has no
// 4-wide window, so only 24 of 782 permutations are batchable (3.1%). Under
// MLDSA_RAM=full it is 484 of 582 (83.2%). The memory budget decides whether
// the lane axis is there at all -- which is the point of being able to set both.
#if defined(PQC_SIMT_KECCAK)
#define MLD_CONFIG_USE_NATIVE_BACKEND_FIPS202
#define MLD_CONFIG_FIPS202_BACKEND_FILE "mld_simt_fips202.h"
#endif

#endif // VORTEX_MLDSA_CONFIG_H
