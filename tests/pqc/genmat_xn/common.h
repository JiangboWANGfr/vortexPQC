#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>

// ML-KEM-768: K=3, so gen_matrix has K*K = 9 algorithmically independent XOF
// streams and the noise stage has 7 (sp0..2, ep0..2, epp) in encaps.
#define GX_ENTRIES 9
#define GX_NOISE   7
#define GX_MAXLANE 16
#define GX_MAXBLK  8   // independent messages, one per warp

typedef struct {
  uint64_t poly_addr;    // out : GX_ENTRIES * 256 * int16_t
  uint64_t noise_addr;   // out : GX_NOISE   * 256 * int16_t
  uint64_t cycles_addr;  // out : 4 * uint64_t  {genmat, noise, blocks, stack}
  uint64_t sum_addr;     // out : 2 * uint32_t  checksums
  uint32_t lanes;        // in
  uint32_t blocks;       // in : independent messages (CTAs)
} kernel_arg_t;

#define GX_PAINT 0xa5a5a5a5u
#endif
