// Per-primitive cost on Vortex, so the ML-KEM profile can be split into the
// parts an accelerator would target.
//
// The library is included here rather than linked: these are internal
// functions, and sharing a translation unit is what gives the benchmark the
// same inlining and code layout the real build gets. Measuring a version the
// compiler treated differently would be measuring the wrong thing.
//
// The per-source includes are what mlkem_native.c itself does, minus the ~570
// lines of #undef it ends with. That cleanup is right for a library consumer --
// it stops internal macros leaking -- but it also removes the mlk_* names this
// benchmark exists to call. MLK_CONFIG_MONOBUILD_KEEP_SHARED_HEADERS does not
// help: it keeps only the parameter-set-independent directives, for multilevel
// builds. Keep this list in step with mlkem_native.c on a submodule bump.
extern "C" {
#include "src/common.h"
#include "src/compress.c"
#include "src/debug.c"
#include "src/indcpa.c"
#include "src/kem.c"
#include "src/poly.c"
#include "src/poly_k.c"
#include "src/sampling.c"
#include "src/verify.c"
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

  auto state = reinterpret_cast<uint64_t*>(scratch + MB_OFF_STATE);
  auto poly  = reinterpret_cast<mlk_poly*>(scratch + MB_OFF_POLY);
  auto seed  = reinterpret_cast<uint8_t*>(scratch + MB_OFF_SEED);

  const uint32_t n = arg->iters;
  uint64_t t0;

  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i)
    mlk_keccakf1600_permute(state);
  cycles[MB_KECCAK_F1600] = vx_rdcycle() - t0;

  // The NTT requires |coeffs| < q on entry and leaves a wider bound, so the
  // input is re-established each iteration. The reduction is inside the timed
  // region and is part of what a native NTT would have to replace anyway.
  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i) {
    for (unsigned j = 0; j < MLKEM_N; ++j)
      poly->coeffs[j] = (int16_t)((j * 7 + i) % MLKEM_Q);
    mlk_poly_ntt(poly);
  }
  cycles[MB_POLY_NTT] = vx_rdcycle() - t0;

  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i)
    mlk_poly_invntt_tomont(poly);
  cycles[MB_POLY_INVNTT] = vx_rdcycle() - t0;

  t0 = vx_rdcycle();
  for (uint32_t i = 0; i < n; ++i) {
    seed[MLKEM_SYMBYTES] = (uint8_t)i;
    mlk_poly_rej_uniform(poly, seed);
  }
  cycles[MB_POLY_REJ_UNIF] = vx_rdcycle() - t0;
}
