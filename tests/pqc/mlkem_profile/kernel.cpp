// Primitive call counts and optional cooperative paths for an ML-KEM-768 round trip.
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

#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_COOP)
#include "mlk_arith_dispatch.h"
#endif

#if defined(PQC_NTT_COOP)
#include "mlkem_coop_ntt.h"
#include "mlk_coop_dispatch.h"

extern "C" __attribute__((noinline, used)) void mlk_profile_main(kernel_arg_t* arg) {
#else
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
#endif
  const unsigned req = blockIdx.x;
  if (req >= arg->requests || threadIdx.x != 0) {
    return;
  }

  auto s      = reinterpret_cast<uint8_t*>(arg->scratch_addr) + req * P_SCRATCH_LEN;
  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr) + req * MLK_PROF_COUNT;
  auto status = reinterpret_cast<int32_t*>(arg->status_addr) + req * 3;
  auto local_counts = mlk_prof_counts[vx_hart_id()];

  for (int i = 0; i < MLK_PROF_COUNT; ++i) {
    local_counts[i] = 0;
  }
  local_counts[MLK_PROF_ARM] = MLK_ARM_EXPECTED;

#if defined(PQC_PROFILE_ARITH)
  for (unsigned i = 0; i < 3; ++i) {
    mlk_arith_cycles[vx_warp_id()][i] = 0;
  }
#endif

#if defined(PQC_NTT_COOP)
  mlk_coop_args[vx_warp_id()].lanes = arg->ntt_lanes;
#endif

  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr) + req * P_CYCLE_COUNT;

  // The arena is a bump allocator whose frees are no-ops, so it has to be reset
  // between operations or the three phases accumulate and the third runs out.
  uint64_t t0 = vx_rdcycle();
  mlk_arena_reset();
  status[0] = mlkem_keypair_derand(s + P_OFF_PK, s + P_OFF_SK, s + P_OFF_COINS_KP);
  uint64_t t1 = vx_rdcycle();
  mlk_arena_reset();
  status[1] = mlkem_enc_derand(s + P_OFF_CT, s + P_OFF_SS_ENC, s + P_OFF_PK,
                               s + P_OFF_COINS_ENC);
  uint64_t t2 = vx_rdcycle();
  mlk_arena_reset();
  status[2] = mlkem_dec(s + P_OFF_SS_DEC, s + P_OFF_CT, s + P_OFF_SK);
  uint64_t t3 = vx_rdcycle();

  cycles[P_CYCLE_KEYPAIR] = t1 - t0;
  cycles[P_CYCLE_ENCAPS] = t2 - t1;
  cycles[P_CYCLE_DECAPS] = t3 - t2;
  cycles[P_CYCLE_START] = t0;
  cycles[P_CYCLE_END] = t3;

#if defined(PQC_PROFILE_ARITH)
  for (unsigned i = 0; i < 3; ++i) {
    cycles[P_CYCLE_MULCACHE + i] = mlk_arith_cycles[vx_warp_id()][i];
  }
#endif

  for (int i = 0; i < MLK_PROF_COUNT; ++i) {
    counts[i] = local_counts[i];
  }
}

#if defined(PQC_NTT_COOP)
// KMU initializes every launched lane before the KEM narrows to its leader.
__kernel __attribute__((naked)) void kernel_main(kernel_arg_t*) {
  asm volatile (
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "tail mlk_profile_main"
      :: "i"(RISCV_CUSTOM0));
}
#endif
