// One ML-DSA-65 forward NTT across L lanes, against the library's own C.
//
// The library sources are included textually, the way tests/pqc/genmat_xn does
// it, for one specific reason: mld_fqmul and mld_zetas are file-scope statics in
// poly.c, and the arith-backend hook is included from common.h long before they
// exist. Pulling the translation unit in puts them in scope, so the cooperative
// transform uses the library's own butterfly arithmetic and the library's own
// twiddle table -- not a copy that could drift from it. What differs between the
// two arms is only which lane does which butterfly.

#include <vx_spawn2.h>
#include <vx_intrinsics.h>

extern "C" {
#include "src/common.h"
#include "src/ct.c"
#include "src/debug.c"
#include "src/poly.c"
#include "src/fips202/fips202.c"
#include "src/fips202/keccakf1600.c"
}

#include "common.h"
#include "pqc_stack.h"

// The transform being shared is the whole point, so it cannot live on the
// stack: vx_start.S gives each hart its own slab, so a stack mld_poly would be
// L private copies and each lane would do 1/L of the butterflies on its own.
// That is not a cooperative NTT, it is L partial ones -- and it reads as a
// speedup, which is why the coefficient-by-coefficient check against the
// library exists. .bss is one address space shared by every hart; one row per
// warp keeps independent warps out of each other's way.
#define NTT_MAX_WARPS 8
static mld_poly ntt_work[NTT_MAX_WARPS];
// The reference arm gets the same storage class as the cooperative one. With it
// on the stack and the shared transform in .bss the two arms differ in memory
// placement as well as in lane mapping, and the comparison stops being about
// the lane mapping.
static mld_poly ntt_ref[NTT_MAX_WARPS];

// Layer `layer` has len = 256 >> layer and exactly 128 butterflies, in
// 256/(2*len) blocks of len each, block b using zetas[2^(layer-1) + b]. ML-DSA
// runs eight layers to ML-KEM's seven -- its NTT is complete, ML-KEM's stops one
// layer early -- so the last layer has len = 1 and 128 blocks of one. The
// reference walks them as a nested loop; this flattens the pair to a single
// index 0..127 so lane t can take t, t+L, t+2L, ... Every butterfly in a layer
// is independent, so the only ordering that matters is between layers.
// Layer `layer` has len = 256 >> layer and exactly 128 butterflies, in
// 256/(2*len) blocks of len each, block b using zetas[2^(layer-1) + b]. ML-DSA
// runs eight layers to ML-KEM's seven -- its NTT is complete, ML-KEM's stops one
// layer early -- so the last layer has len = 1 and 128 blocks of one. The
// reference walks that as a nested loop; this flattens the pair to one index
// 0..127 so lane t takes t, t+L, t+2L, ... Every butterfly within a layer is
// independent, so the only ordering that matters is between layers.
//
// A two-regime variant was tried and is slower where it counts: splitting the
// inner loop when len >= L and whole blocks when len < L keeps zeta loop
// invariant and wins at one lane (190,360 against 211,510 cycles), but loses at
// four (57,830 against 53,686), which is the operating point. Recorded here so
// the choice does not look arbitrary, and because "the obvious optimisation is
// the wrong one above L=2" is the kind of thing worth knowing before designing
// the instruction.
static void ntt_coop(int32_t *r, unsigned L, unsigned tid) {
  for (unsigned layer = 1; layer <= 8; ++layer) {
    const unsigned lg  = 8u - layer;      // len = 1 << lg
    const unsigned len = 1u << lg;
    const unsigned k0  = 1u << (layer - 1);
    for (unsigned b = tid; b < NTT_N / 2; b += L) {
      const unsigned block = b >> lg;
      const unsigned jj    = b & (len - 1u);
      const unsigned j     = (block << (lg + 1)) + jj;
      const int32_t zeta   = mld_zetas[k0 + block];
      const int32_t t      = mld_fqmul(r[j + len], zeta);
      r[j + len] = (int32_t)(r[j] - t);
      r[j]       = (int32_t)(r[j] + t);
    }
    // Lanes of a warp are lockstep, so no barrier is needed -- but the writes
    // must be visible to the other lanes before the next layer reads them.
    vx_fence();
  }
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto out    = reinterpret_cast<int32_t*>(arg->poly_addr);
  auto ref    = reinterpret_cast<int32_t*>(arg->ref_addr);
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);
  auto mism   = reinterpret_cast<uint32_t*>(arg->mism_addr);

  const unsigned tid = (unsigned)vx_thread_id();
  const unsigned L   = arg->lanes;

  uint32_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

  const unsigned wid = (unsigned)vx_warp_id() & (NTT_MAX_WARPS - 1);
  mld_poly *a = &ntt_work[wid];
  mld_poly *b = &ntt_ref[wid];

  // Same deterministic input for both arms, inside the +-q bound the transform
  // requires on entry. Every lane writes the same values to the same shared
  // array -- idempotent, so no barrier is needed to agree on it, only a fence
  // to make it visible before the transform starts reading across lanes.
  for (unsigned i = 0; i < NTT_N; ++i) {
    const int32_t v = (int32_t)((int)(i * 3121u % (2u * MLDSA_Q)) - MLDSA_Q + 1);
    a->coeffs[i] = v;
    b->coeffs[i] = v;
  }
  vx_fence();

  const uint64_t t0 = vx_rdcycle();
  ntt_coop(a->coeffs, L, tid);
  const uint64_t t1 = vx_rdcycle();

  // The reference runs on one lane: it is the L=1 denominator, and running it
  // redundantly on all of them would measure the same thing L times.
  if (tid == 0) {
    const uint64_t t2 = vx_rdcycle();
    mld_poly_ntt(b);
    const uint64_t t3 = vx_rdcycle();

    uint32_t bad = 0;
    for (unsigned i = 0; i < NTT_N; ++i) {
      out[i] = a->coeffs[i];
      ref[i] = b->coeffs[i];
      if (a->coeffs[i] != b->coeffs[i]) ++bad;
    }
    mism[0] = bad;

    cycles[NTT_CY_COOP]  = t1 - t0;
    cycles[NTT_CY_REF]   = t3 - t2;
    cycles[NTT_CY_SPAN]  = span;
    cycles[NTT_CY_STACK] = pqc_stack_watermark(sp0);
  }
}
