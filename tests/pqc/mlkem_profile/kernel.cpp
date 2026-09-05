// Exact primitive call counts for one ML-KEM-768 round trip.
//
// The library is configured with two counting backends (see
// vortex_prof_config.h): each hook records the call and returns
// MLK_NATIVE_FUNC_FALLBACK, so the C implementation still does the work. The
// run is therefore the baseline run, with counters attached -- not a different
// build that happens to resemble it.
//
// Sources are included per-file rather than through mlkem_native.c so the
// counter array stays reachable; see tests/pqc/mlkem_microbench/kernel.cpp for why
// the monolithic wrapper cannot be used here.
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

// Public entry points. The internal mlk_kem_* names are function-like macros
// carrying the optional context parameter, and this test wants the same three
// calls the mlkem test makes anyway.
#include "mlkem_native.h"
}

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  auto s      = reinterpret_cast<uint8_t*>(arg->scratch_addr);
  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr);
  auto status = reinterpret_cast<int32_t*>(arg->status_addr);

  for (int i = 0; i < MLK_PROF_COUNT; ++i)
    mlk_prof_counts[i] = 0;

  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);

  uint64_t t0 = vx_rdcycle();
  status[0] = mlkem_keypair_derand(s + P_OFF_PK, s + P_OFF_SK, s + P_OFF_COINS_KP);
  uint64_t t1 = vx_rdcycle();
  status[1] = mlkem_enc_derand(s + P_OFF_CT, s + P_OFF_SS_ENC, s + P_OFF_PK,
                               s + P_OFF_COINS_ENC);
  uint64_t t2 = vx_rdcycle();
  status[2] = mlkem_dec(s + P_OFF_SS_DEC, s + P_OFF_CT, s + P_OFF_SK);
  uint64_t t3 = vx_rdcycle();

  cycles[0] = t1 - t0;
  cycles[1] = t2 - t1;
  cycles[2] = t3 - t2;

  for (int i = 0; i < MLK_PROF_COUNT; ++i)
    counts[i] = mlk_prof_counts[i];
}
