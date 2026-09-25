// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Bump allocator backing mldsa-native's MLD_CUSTOM_ALLOC on Vortex.
//
// ML-DSA does not fit the default stack allocation here. Signing holds two
// polyvecs, a yvec and several polys at once against VX_MEM_STACK_LOG2_SIZE =
// 13, an 8 KB per-hart slab. (MLD_CONFIG_REDUCE_RAM selects the lazy matrix,
// mld_polymat_lazy, which is 256 * 4 + 32 = 1,056 bytes, not the 30,720 of a
// materialised 6x5 mld_polymat -- the pressure is the rest of the working set,
// not the matrix.) So the library's allocation hook is not an optimization
// here, it is what makes ML-DSA run at all.
//
// The arena is a file-scope array, which the linker places in device memory
// rather than on the stack. The pinned library frees in reverse allocation
// order, including each rejected signing attempt. Reclaiming those frames
// keeps the live working set independent of the number of retries.
//
// Each independent request needs its own arena. Warp-request kernels allocate
// only on their leader; lane-request kernels need one arena per hart.

#ifndef MLD_VORTEX_ALLOC_H
#define MLD_VORTEX_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#define MLD_HARTS (VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES * \
                   VX_CFG_NUM_WARPS * VX_CFG_NUM_THREADS)
// Materializing A needs a larger budget than lazy matrix expansion.
#if defined(PQC_MLDSA_RAM_FULL)
#if MLD_CONFIG_PARAMETER_SET == 87
#define MLD_ARENA_BYTES 163840u
#else
#define MLD_ARENA_BYTES 90112u
#endif
#else
#if MLD_CONFIG_PARAMETER_SET == 87
#define MLD_ARENA_BYTES 32768u
#else
#define MLD_ARENA_BYTES 24576u
#endif
#endif
#define MLD_ARENA_ALIGN 32u
#if defined(PQC_MLDSA_WARP_REQUEST)
#define MLD_ARENA_SLOTS (MLD_HARTS / VX_CFG_NUM_THREADS)
#else
#define MLD_ARENA_SLOTS MLD_HARTS
#endif

#if defined(__VORTEX__)

// Included here rather than relied on from the command line so the header is
// self-contained: the hart-count keys are only used below this guard.
#include <VX_config.h>
#include <vx_intrinsics.h>

static uint8_t mld_arena[MLD_ARENA_SLOTS][MLD_ARENA_BYTES]
    __attribute__((aligned(MLD_ARENA_ALIGN)));
static uint32_t mld_arena_top[MLD_ARENA_SLOTS];
static uint32_t mld_arena_peak[MLD_ARENA_SLOTS];
static uint32_t mld_arena_fail[MLD_ARENA_SLOTS];

static inline uint32_t mld_arena_index(void)
{
#if defined(PQC_MLDSA_WARP_REQUEST)
  return (uint32_t)vx_hart_id() / VX_CFG_NUM_THREADS;
#else
  return (uint32_t)vx_hart_id();
#endif
}

static inline void *mld_arena_alloc(uint32_t bytes)
{
  const uint32_t t = mld_arena_index();
  uint32_t base;
  // Out-of-range would alias another hart's arena, which is the bug this
  // header exists to prevent; fail instead.
  if (t >= MLD_ARENA_SLOTS)
    return NULL;
  base = (mld_arena_top[t] + (MLD_ARENA_ALIGN - 1u)) & ~(MLD_ARENA_ALIGN - 1u);
  if (base + bytes > MLD_ARENA_BYTES)
  {
    // NULL is the library's failure path: MLD_FREE guards on it and every
    // caller returns MLD_ERR_OUT_OF_MEMORY, which lands in status[] and fails
    // the test. Handing back the arena base instead would corrupt live data.
    mld_arena_fail[t]++;
    return NULL;
  }
  mld_arena_top[t] = (base + bytes + MLD_ARENA_ALIGN - 1u) & ~(MLD_ARENA_ALIGN - 1u);
  if (base + bytes > mld_arena_peak[t])
    mld_arena_peak[t] = base + bytes;
  return &mld_arena[t][base];
}

static inline void mld_arena_free(void *ptr, uint32_t bytes)
{
  const uint32_t t = mld_arena_index();
  if (ptr == NULL || t >= MLD_ARENA_SLOTS)
    return;
  const uint32_t base = (uint8_t *)ptr - mld_arena[t];
  const uint32_t end = (base + bytes + MLD_ARENA_ALIGN - 1u) & ~(MLD_ARENA_ALIGN - 1u);
  if (end != mld_arena_top[t])
  {
    mld_arena_fail[t]++;
    return;
  }
  mld_arena_top[t] = base;
}

static inline void mld_arena_reset(void)
{
  const uint32_t t = mld_arena_index();
  if (t < MLD_ARENA_SLOTS)
    mld_arena_top[t] = 0;
}

#endif /* __VORTEX__ */
#endif /* MLD_VORTEX_ALLOC_H */
