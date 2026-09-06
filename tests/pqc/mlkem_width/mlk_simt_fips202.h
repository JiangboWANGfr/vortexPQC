// SIMT FIPS-202 backend: the x4-batched Keccak-f1600 spread over the lanes of
// one Vortex warp, one Keccak state per lane.
//
// This is the thing the analytic "slot" model assumes exists. The library's
// default x4 permutation is a four-iteration serial loop, so the x4 API alone
// buys nothing on a scalar lane; the batch only becomes a batch when the four
// states land on four lanes. mlk_keccak_f1600_x4_native() is the library's own
// drop-in hook, so the submodule stays byte-for-byte pristine.
//
// The exchange buffer is a file-scope array. Every hart shares one address
// space (only the stack is hart-indexed, VX_MEM_STACK_LOG2_SIZE), so a global
// is warp-wide storage; the per-lane value is the CSR-read thread id, never a
// global, which would be one location W lanes race on.
#ifndef MLK_SIMT_FIPS202_H
#define MLK_SIMT_FIPS202_H

#if !defined(__ASSEMBLER__)
#include "src/fips202/native/api.h"
#include "mlk_width_counters.h"

// The library's own serial permutation. Declared rather than #included: this
// file is pulled in from src/common.h, before keccakf1600.h exists. Same
// namespaced symbol, same translation unit, so this is the very code the
// baseline runs -- the only difference between the arms is which lane runs it.
#define mlkw_permute1 MLK_NAMESPACE(keccakf1600_permute)
extern void mlkw_permute1(uint64_t *state);

// Warp-wide staging for the four permuted states. One row set per warp, so
// independent messages running on other warps do not collide here.
#define MLKW_MAX_WARPS 8
static uint64_t mlkw_xbuf[MLKW_MAX_WARPS][4][25];

// Lanes the launch made active. One shared location, written once by every
// lane with the same value before the KEM starts, so the race is benign.
static unsigned mlkw_lanes;

#define MLK_USE_NATIVE_FIPS202_X1
static MLK_INLINE int mlk_keccak_f1600_x1_native(uint64_t *state)
{
  (void)state;
  mlkw_counts[MLKW_KECCAK_X1]++;
  return MLK_NATIVE_FUNC_FALLBACK;
}

#define MLK_USE_NATIVE_FIPS202_X4
static MLK_INLINE int mlk_keccak_f1600_x4_native(uint64_t *state)
{
  const unsigned tid = (unsigned)vx_thread_id();
  uint64_t (*xb)[25] = mlkw_xbuf[(unsigned)vx_warp_id() & (MLKW_MAX_WARPS - 1)];
  const unsigned W   = mlkw_lanes;
  unsigned s, i;

  mlkw_counts[MLKW_KECCAK_X4]++;
  mlkw_counts[MLKW_SLOTS] += (4 + W - 1) / W;

  // Lane t takes sub-states t, t+W, t+2W, ... Every lane runs the same loop,
  // so W >= 4 is one iteration each (one slot) and W = 1 is four iterations
  // (four slots) -- the baseline, reproduced exactly by the same source.
  for (s = tid; s < 4; s += W)
  {
    uint64_t tmp[25];
    for (i = 0; i < 25; i++) tmp[i] = state[25 * s + i];
    mlkw_permute1(tmp);
    for (i = 0; i < 25; i++) xb[s][i] = tmp[i];
  }

  // Lanes wrote disjoint rows; make them visible before every lane reads all
  // four back into its own private copy of the state.
  vx_fence();

  for (s = 0; s < 4; s++)
    for (i = 0; i < 25; i++) state[25 * s + i] = xb[s][i];

  return MLK_NATIVE_FUNC_SUCCESS;
}

#endif /* !__ASSEMBLER__ */
#endif
