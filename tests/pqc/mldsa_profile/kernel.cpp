// The library is included here rather than compiled as its own translation
// unit, because the arena in mld_vortex_alloc.h is file-scope state that both
// sides must share. With two TUs each gets its own copy: the library allocates
// from one and the kernel reports the other, which reads back as a peak of
// zero while the results are perfectly correct -- a silent instrument failure,
// not a wrong answer.
extern "C" {
#include "mldsa_native.c"
}

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"
#include "pqc_stack.h"
#include "mld_prof_counters.h"
#include "mld_vortex_alloc.h"

#if defined(PQC_KECCAK_SG25)
#include "mld_sg25.h"
#endif
#if defined(PQC_NTT_REG32)
#include "mld_ntt_reg32.h"
#endif
#if defined(PQC_REJ_WARP)
#include "mld_rej_warp.h"
#endif
#if defined(PQC_POINTWISE_ISE) || defined(PQC_POINTWISE_L5_W32)
#include "mld_pointwise_w32.h"
#endif
#if defined(PQC_SIGN_DECOMPOSE_WARP) || defined(PQC_SIGN_HINT_WARP) || \
    defined(PQC_SIGN_CHKNORM_WARP) || defined(PQC_SIGN_CADDQ_WARP) || \
    defined(PQC_SIGN_ZUNPACK_WARP)
#include "mld_sign_w32.h"
#endif
#if defined(PQC_ZEROIZE_WARP)
#include "mld_zeroize_w32.h"
#endif

extern "C" __attribute__((noinline, used)) void mld_profile_main(kernel_arg_t* arg) {
  for (unsigned req = blockIdx.x; req < arg->requests; req += arg->workers) {

  auto seed   = reinterpret_cast<const uint8_t*>(arg->seed_addr) + req * MLDSA_SEEDBYTES;
  auto rnd    = reinterpret_cast<const uint8_t*>(arg->rnd_addr) + req * MLDSA_RNDBYTES;
  auto msg    = reinterpret_cast<const uint8_t*>(arg->msg_addr) + req * MLDSA_MSG_BYTES;
  auto pk     = reinterpret_cast<uint8_t*>(arg->pk_addr) + req * MLDSA_PK_BYTES;
  auto sk     = reinterpret_cast<uint8_t*>(arg->sk_addr) + req * MLDSA_SK_BYTES;
  auto sig    = reinterpret_cast<uint8_t*>(arg->sig_addr) + req * MLDSA_SIG_BYTES;
  auto status = reinterpret_cast<int32_t*>(arg->status_addr) + req * MLDSA_ST_COUNT;
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr) + req * MLDSA_CY_COUNT;
  auto arena  = reinterpret_cast<uint32_t*>(arg->arena_addr) + req * 4;
  auto pointwise_cycles = reinterpret_cast<uint64_t*>(arg->pointwise_cycles_addr)
                        + req * MLDSA_PROFILE_CY_COUNT;

  uintptr_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

  auto local_counts = mld_prof_counts[mld_prof_slot()];
  for (int i = 0; i < MLD_PROF_COUNT; ++i) {
    local_counts[i] = 0;
  }
  local_counts[MLD_PROF_ARM] = MLD_ARM_EXPECTED;
  const unsigned slot = mld_prof_slot();
#if defined(PQC_ZEROIZE_WARP)
  mld_zeroize_parallel[slot] = arg->workers == 1;
#endif
  for (unsigned phase = 0; phase < MLDSA_ST_COUNT; ++phase)
    for (unsigned kind = 0; kind < 2; ++kind)
      mld_prof_pointwise_cycles[slot][phase][kind] = 0;
#if defined(PQC_PROFILE_PHASES)
  for (unsigned phase = 0; phase < MLDSA_ST_COUNT; ++phase)
    for (unsigned kind = 0; kind < MLD_PHASE_COUNT; ++kind)
      mld_prof_detail_cycles[slot][phase][kind] = 0;
#endif

  // Setup and diagnostics must not overlap another resident request's timing.
  vx_barrier(1u << 8, arg->workers);
  uint64_t t0 = vx_rdcycle();
  mld_prof_phase[slot] = MLDSA_ST_KEYPAIR;
  mld_arena_reset();
  status[MLDSA_ST_KEYPAIR] = mldsa_keypair_internal(pk, sk, seed);

  uint64_t t1 = vx_rdcycle();
#if defined(PQC_KEYPAIR_ONLY)
  // Signing is a rejection loop: with an ablated primitive the candidate never
  // passes its norm check, so the loop runs to max_signing_attempts every time
  // and the ablated build does far MORE work than the baseline -- its delta
  // would measure the retry explosion, not the primitive. Verification then has
  // no valid signature to check. Keypair is the one ML-DSA phase whose control
  // flow does not depend on the values, so it is the one that can be ablated.
  (void)sig; (void)msg; (void)rnd;
  status[MLDSA_ST_SIGN] = 0;
  status[MLDSA_ST_VERIFY] = 0;
  uint64_t t2 = t1, t3 = t1;
#else
  mld_prof_phase[slot] = MLDSA_ST_SIGN;
  mld_arena_reset();
  status[MLDSA_ST_SIGN] = mldsa_signature_internal(sig, msg, MLDSA_MSG_BYTES,
                                                   nullptr, 0, rnd, sk, 0);

  uint64_t t2 = vx_rdcycle();
#if defined(PQC_STOP_AFTER_SIGN)
  status[MLDSA_ST_VERIFY] = 0;
  uint64_t t3 = t2;
#else
  mld_prof_phase[slot] = MLDSA_ST_VERIFY;
  mld_arena_reset();
  status[MLDSA_ST_VERIFY] = mldsa_verify_internal(sig, msg, MLDSA_MSG_BYTES,
                                                  nullptr, 0, pk, 0);
  uint64_t t3 = vx_rdcycle();
#endif
#endif

  vx_barrier(1u << 8, arg->workers);
  cycles[MLDSA_CY_START] = t0;
  cycles[MLDSA_CY_END] = t3;

  cycles[MLDSA_CY_KEYPAIR] = t1 - t0;
  cycles[MLDSA_CY_SIGN]    = t2 - t1;
  cycles[MLDSA_CY_VERIFY]  = t3 - t2;

  const uint32_t h = mld_arena_index();
  arena[0] = mld_arena_peak[h];
  arena[1] = mld_arena_fail[h];
  arena[2] = pqc_stack_watermark(sp0);
  arena[3] = span;

  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr) + req * MLD_PROF_COUNT;
  for (int i = 0; i < MLD_PROF_COUNT; ++i) {
    counts[i] = local_counts[i];
  }
  for (unsigned phase = 0; phase < MLDSA_ST_COUNT; ++phase)
    for (unsigned kind = 0; kind < 2; ++kind)
      pointwise_cycles[phase * 2 + kind] =
          mld_prof_pointwise_cycles[slot][phase][kind];
#if defined(PQC_PROFILE_PHASES)
  for (unsigned phase = 0; phase < MLDSA_ST_COUNT; ++phase)
    for (unsigned kind = 0; kind < MLD_PHASE_COUNT; ++kind)
      pointwise_cycles[MLDSA_POINTWISE_CY_COUNT + phase * MLD_PHASE_COUNT + kind] =
          mld_prof_detail_cycles[slot][phase][kind];
#endif
  }
}

__kernel __attribute__((naked)) void kernel_main(kernel_arg_t*) {
  asm volatile (
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "tail mld_profile_main"
      :: "i"(RISCV_CUSTOM0));
}
