// Per-primitive cost for ML-DSA, the counterpart of tests/pqc/mlkem_microbench.
//
// Sources are included per-file for the same reason as there: mldsa_native.c
// ends with an #undef block that removes the mld_* names this benchmark calls.
// Keep this list in step with mldsa_native.c on a submodule bump.
extern "C" {
#include "src/common.h"
#include "src/ct.c"
#include "src/debug.c"
#include "src/packing.c"
#include "src/poly.c"
#include "src/poly_kl.c"
#include "src/polyvec.c"
#include "src/polyvec_lazy.c"
#include "src/sign.c"
#include "src/fips202/fips202.c"
#include "src/fips202/fips202x4.c"
#include "src/fips202/keccakf1600.c"
}

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  auto cycles  = reinterpret_cast<uint64_t*>(arg->cycles_addr);
  auto scratch = reinterpret_cast<uint8_t*>(arg->scratch_addr);
  auto state   = reinterpret_cast<uint64_t*>(scratch + MB_OFF_STATE);
  auto poly    = reinterpret_cast<mld_poly*>(scratch + MB_OFF_POLY);

  const uint32_t n = arg->iters;
  uint64_t t0;

  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i)
    mld_keccakf1600_permute(state);
  cycles[MB_KECCAK_F1600] = vx_rdcycle() - t0;

  // Re-establish a bounded input each iteration, as the ML-KEM benchmark does:
  // the transform widens the coefficient bound, so feeding its own output back
  // would drift out of the range the next call requires.
  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i) {
    for (unsigned j = 0; j < MLDSA_N; ++j)
      poly->coeffs[j] = (int32_t)((j * 7 + i) % MLDSA_Q);
    mld_poly_ntt(poly);
  }
  cycles[MB_POLY_NTT] = vx_rdcycle() - t0;

  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i)
    mld_poly_invntt_tomont(poly);
  cycles[MB_POLY_INVNTT] = vx_rdcycle() - t0;
}
