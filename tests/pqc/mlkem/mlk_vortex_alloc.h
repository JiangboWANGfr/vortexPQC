// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

// Per-hart bump allocator backing mlkem-native's MLK_CUSTOM_ALLOC on Vortex.
//
// ML-KEM-768 does not fit the default stack allocation here. Measured with
// -fstack-usage on this tree: mlkem_indcpa_enc alone is a 13,184-byte frame,
// indcpa_keypair_derand 10,208 and indcpa_dec 4,928, against an 8,192-byte
// per-hart slab (VX_MEM_STACK_LOG2_SIZE = 13). The single-lane baseline ran
// anyway only because harts 1..N-1 were idle and absorbed the overflow -- see
// tests/pqc/pqc_stack.h, whose probe now fails the test instead.
//
// SG25 is an exception to the per-hart layout: lane 0 runs one logical request
// and temporarily activates the warp only inside FIPS-202, so it uses one arena
// per warp. Other builds retain one arena per hart.
//
// Freeing is a no-op: the library does not release in LIFO order, and one
// operation is short enough that growing to the peak and resetting between
// operations is simpler and costs nothing measurable. The consequence is that
// the high-water mark is the SUM of an operation's live allocations, not its
// maximum nesting depth -- decaps sums to 19,232 bytes, which is the figure to
// budget per arena (one hart normally, one warp for SG25).
//
// .bss cost is MLK_ARENA_SLOTS * MLK_ARENA_BYTES, zero-filled by the loader at
// every launch. Slots are harts normally and warps for SG25.

#ifndef MLK_VORTEX_ALLOC_H
#define MLK_VORTEX_ALLOC_H

#include <stddef.h>
#include <stdint.h>

// csr_unit.cpp: mhartid = (global_core_id * NUM_WARPS + wid) * NUM_THREADS + tid,
// and total cores = NUM_CLUSTERS * NUM_SOCKETS * SOCKET_SIZE. All four keys are
// projected into the build by the config system.
#define MLK_HARTS (VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES * \
                   VX_CFG_NUM_WARPS * VX_CFG_NUM_THREADS)
#if defined(PQC_KECCAK_SG25)
#define MLK_ARENA_SLOTS (MLK_HARTS / VX_CFG_NUM_THREADS)
#else
#define MLK_ARENA_SLOTS MLK_HARTS
#endif

// Measured peak is 19,232 bytes (decaps); sized with headroom, and the reported
// peak is checked against this so a parameter-set change cannot silently
// overflow it.
#define MLK_ARENA_BYTES 24576u
#define MLK_ARENA_ALIGN 32u

#if defined(__VORTEX__)

// Included here rather than relied on from the command line so the header is
// self-contained: the hart-count keys are only used below this guard.
#include <VX_config.h>
#include <vx_intrinsics.h>

static uint8_t mlk_arena[MLK_ARENA_SLOTS][MLK_ARENA_BYTES]
    __attribute__((aligned(MLK_ARENA_ALIGN)));
static uint32_t mlk_arena_top[MLK_ARENA_SLOTS];
static uint32_t mlk_arena_peak[MLK_ARENA_SLOTS];
static uint32_t mlk_arena_fail[MLK_ARENA_SLOTS];

static inline uint32_t mlk_arena_index(void)
{
#if defined(PQC_KECCAK_SG25)
  return (uint32_t)vx_hart_id() / VX_CFG_NUM_THREADS;
#else
  return (uint32_t)vx_hart_id();
#endif
}

static inline void *mlk_arena_alloc(uint32_t bytes)
{
  const uint32_t t = mlk_arena_index();
  uint32_t base;
  // Out-of-range would alias another hart's arena, which is the bug this
  // header exists to prevent; fail instead.
  if (t >= MLK_ARENA_SLOTS)
    return NULL;
  base = (mlk_arena_top[t] + (MLK_ARENA_ALIGN - 1u)) & ~(MLK_ARENA_ALIGN - 1u);
  if (base + bytes > MLK_ARENA_BYTES)
  {
    // NULL is the library's documented failure path: every caller checks it
    // and returns MLK_ERR_OUT_OF_MEMORY, which lands in status[] and fails the
    // test. The counter says how many times, so a near-miss is visible too.
    mlk_arena_fail[t]++;
    return NULL;
  }
  mlk_arena_top[t] = base + bytes;
  if (mlk_arena_top[t] > mlk_arena_peak[t])
    mlk_arena_peak[t] = mlk_arena_top[t];
  return &mlk_arena[t][base];
}

static inline void mlk_arena_reset(void)
{
  const uint32_t t = mlk_arena_index();
  if (t < MLK_ARENA_SLOTS)
    mlk_arena_top[t] = 0;
}

#define MLK_CUSTOM_ALLOC(v, T, N) \
  T *(v) = (T *)mlk_arena_alloc((uint32_t)(sizeof(T) * (N)))
#define MLK_CUSTOM_FREE(v, T, N) \
  do { (void)sizeof(T); (void)(N); } while (0)

#endif /* __VORTEX__ */
#endif /* MLK_VORTEX_ALLOC_H */
