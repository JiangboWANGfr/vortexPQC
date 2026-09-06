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
// rather than on the stack. Freeing is a no-op: allocations are not released
// in LIFO order, and a single KEM/signature operation is short enough that
// growing to the peak and resetting between operations is simpler and has no
// measurable cost. mld_arena_peak records that peak, which is a result in its
// own right -- it is the working-set figure any hardware design has to budget
// for.
//
// One arena per hart, indexed by mhartid: a single shared arena would trade
// the stack race for an arena race the moment a second lane runs.

#ifndef MLD_VORTEX_ALLOC_H
#define MLD_VORTEX_ALLOC_H

#include <stddef.h>
#include <stdint.h>

// Same hart count and size as the ML-KEM arena (tests/pqc/mlkem/
// mlk_vortex_alloc.h): one number for both schemes. Measured ML-DSA-65 peak is
// 21,568 bytes, so 24 KB leaves 12% headroom; 128 KB per hart would be 2 MB of
// .bss the loader zero-fills at every launch.
#define MLD_HARTS (VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES * \
                   VX_CFG_NUM_WARPS * VX_CFG_NUM_THREADS)
#define MLD_ARENA_BYTES 24576u
#define MLD_ARENA_ALIGN 32u

#if defined(__VORTEX__)

// Included here rather than relied on from the command line so the header is
// self-contained: the hart-count keys are only used below this guard.
#include <VX_config.h>
#include <vx_intrinsics.h>

static uint8_t mld_arena[MLD_HARTS][MLD_ARENA_BYTES]
    __attribute__((aligned(MLD_ARENA_ALIGN)));
static uint32_t mld_arena_top[MLD_HARTS];
static uint32_t mld_arena_peak[MLD_HARTS];
static uint32_t mld_arena_fail[MLD_HARTS];

static inline void *mld_arena_alloc(uint32_t bytes)
{
  const uint32_t t = (uint32_t)vx_hart_id();
  uint32_t base;
  // Out-of-range would alias another hart's arena, which is the bug this
  // header exists to prevent; fail instead.
  if (t >= MLD_HARTS)
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
  mld_arena_top[t] = base + bytes;
  if (mld_arena_top[t] > mld_arena_peak[t])
    mld_arena_peak[t] = mld_arena_top[t];
  return &mld_arena[t][base];
}

static inline void mld_arena_reset(void)
{
  const uint32_t t = (uint32_t)vx_hart_id();
  if (t < MLD_HARTS)
    mld_arena_top[t] = 0;
}

#endif /* __VORTEX__ */
#endif /* MLD_VORTEX_ALLOC_H */
