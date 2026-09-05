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
#include "mld_prof_counters.h"
#include "mld_vortex_alloc.h"

// ML-DSA-65 keypair -> sign -> verify, one thread. Same shape and same reasons
// as the ML-KEM baseline: single-threaded, so the denominator every later
// speedup divides into does not also carry a parallelisation decision.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  auto seed   = reinterpret_cast<const uint8_t*>(arg->seed_addr);
  auto rnd    = reinterpret_cast<const uint8_t*>(arg->rnd_addr);
  auto msg    = reinterpret_cast<const uint8_t*>(arg->msg_addr);
  auto pk     = reinterpret_cast<uint8_t*>(arg->pk_addr);
  auto sk     = reinterpret_cast<uint8_t*>(arg->sk_addr);
  auto sig    = reinterpret_cast<uint8_t*>(arg->sig_addr);
  auto status = reinterpret_cast<int32_t*>(arg->status_addr);
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);
  auto arena  = reinterpret_cast<uint32_t*>(arg->arena_addr);

  // The arena is not freed in LIFO order, so it is reset between operations
  // rather than unwound; the peak across all three is what gets reported.
  for (int i = 0; i < MLD_PROF_COUNT; ++i)
    mld_prof_counts[i] = 0;

  uint64_t t0 = vx_rdcycle();
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
  mld_arena_reset();
  status[MLDSA_ST_SIGN] = mldsa_signature_internal(sig, msg, MLDSA_MSG_BYTES,
                                                   nullptr, 0, rnd, sk, 0);

  uint64_t t2 = vx_rdcycle();
  mld_arena_reset();
  status[MLDSA_ST_VERIFY] = mldsa_verify_internal(sig, msg, MLDSA_MSG_BYTES,
                                                  nullptr, 0, pk, 0);
  uint64_t t3 = vx_rdcycle();
#endif

  cycles[MLDSA_CY_KEYPAIR] = t1 - t0;
  cycles[MLDSA_CY_SIGN]    = t2 - t1;
  cycles[MLDSA_CY_VERIFY]  = t3 - t2;

  arena[0] = mld_arena_peak;
  arena[1] = mld_arena_fail;

  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr);
  for (int i = 0; i < MLD_PROF_COUNT; ++i)
    counts[i] = mld_prof_counts[i];
}
