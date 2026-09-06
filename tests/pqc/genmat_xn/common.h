#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>
#include <VX_config.h>

// ML-KEM-768: K=3, so gen_matrix has K*K = 9 algorithmically independent XOF
// streams and the noise stage has 7 (sp0..2, ep0..2, epp) in encaps.
#define GX_ENTRIES 9
#define GX_NOISE   7

// Every hart that can exist in this build, so the per-hart XOF-block counters
// have a slot each. A single shared counter is a read-modify-write race across
// both the lanes of a warp and the CTAs of a later wave.
#define GX_HARTS (VX_CFG_NUM_CLUSTERS * VX_CFG_NUM_CORES * \
                  VX_CFG_NUM_WARPS * VX_CFG_NUM_THREADS)

// Checksums of CTA 0's output. CTA b samples with seed[0] ^= b, so CTA 0's
// result is the same at every width and every message count -- which is the
// invariant this test exists to assert, and the reason it can be a constant.
#define GX_SUM_POLY  0x817bf6c4u
#define GX_SUM_NOISE 0x104bd412u

typedef struct {
  uint64_t poly_addr;    // out : blocks * GX_ENTRIES * 256 * int16_t
  uint64_t noise_addr;   // out : blocks * GX_NOISE   * 256 * int16_t
  uint64_t cycles_addr;  // out : 4 * uint64_t  {genmat, noise, span, stack peak}
  uint64_t sum_addr;     // out : 2 * uint32_t  checksums of CTA 0
  uint64_t xof_addr;     // out : GX_HARTS * uint32_t, XOF blocks per hart
  uint32_t lanes;        // in
  uint32_t blocks;       // in : independent messages (CTAs)
} kernel_arg_t;
#endif
