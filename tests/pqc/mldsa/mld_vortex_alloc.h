// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Bump allocator backing mldsa-native's MLD_CUSTOM_ALLOC on Vortex.
//
// ML-DSA does not fit the default stack allocation here. For ML-DSA-65 (K=6,
// L=5) one mld_polymat alone is 6 * 5 * 256 * 4 = 30,720 bytes, and signing
// holds a polymat, two polyvecs, a yvec and several polys at once -- against
// VX_MEM_STACK_LOG2_SIZE = 13, an 8 KB per-thread stack. So the library's
// allocation hook is not an optimization here, it is what makes ML-DSA run at
// all.
//
// The arena is a file-scope array, which the linker places in device memory
// rather than on the stack. Freeing is a no-op: allocations are not released
// in LIFO order, and a single KEM/signature operation is short enough that
// growing to the peak and resetting between operations is simpler and has no
// measurable cost. mld_arena_peak records that peak, which is a result in its
// own right -- it is the working-set figure any hardware design has to budget
// for.
//
// Single-threaded by construction: the baseline kernel runs one lane, and a
// file-scope arena would need per-thread partitioning otherwise.

#ifndef MLD_VORTEX_ALLOC_H
#define MLD_VORTEX_ALLOC_H

#include <stddef.h>
#include <stdint.h>

// Sized from the measured peak with headroom; mld_arena_peak reports the
// actual high-water mark so this can be tightened once it is known.
#define MLD_ARENA_BYTES (128 * 1024)
#define MLD_ARENA_ALIGN 32

#if defined(__VORTEX__)

static uint8_t mld_arena[MLD_ARENA_BYTES] __attribute__((aligned(MLD_ARENA_ALIGN)));
static uint32_t mld_arena_top;
static uint32_t mld_arena_peak;
static uint32_t mld_arena_fail;

static inline void *mld_arena_alloc(uint32_t bytes)
{
  uint32_t base = (mld_arena_top + (MLD_ARENA_ALIGN - 1)) & ~(uint32_t)(MLD_ARENA_ALIGN - 1);
  if (base + bytes > MLD_ARENA_BYTES)
  {
    // Returning NULL would have the library dereference it. Failing loudly via
    // a counter the host reads is more useful than a hang in device memory.
    mld_arena_fail++;
    return mld_arena;
  }
  mld_arena_top = base + bytes;
  if (mld_arena_top > mld_arena_peak)
    mld_arena_peak = mld_arena_top;
  return mld_arena + base;
}

static inline void mld_arena_reset(void) { mld_arena_top = 0; }

#endif /* __VORTEX__ */
#endif /* MLD_VORTEX_ALLOC_H */
