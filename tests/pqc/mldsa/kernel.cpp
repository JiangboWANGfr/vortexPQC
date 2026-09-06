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
#include "mld_vortex_alloc.h"

// ML-DSA-65 keypair -> sign -> verify, one thread. Same shape and same reasons
// as the ML-KEM baseline: single-threaded, so the denominator every later
// speedup divides into does not also carry a parallelisation decision.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned req = blockIdx.x;
  // One lane per request, for the same reason as the ML-KEM batch: this build
  // is the pristine library, so there is no lane-cooperative work to hand a
  // second lane. block_dim is 1 and grid_dim is exactly `requests`, so both
  // guards only fire if a harness rounds the launch up.
  if (threadIdx.x != 0 || req >= arg->requests)
    return;

  auto seed   = reinterpret_cast<const uint8_t*>(arg->seed_addr);
  auto rnd    = reinterpret_cast<const uint8_t*>(arg->rnd_addr);
  auto msg    = reinterpret_cast<const uint8_t*>(arg->msg_addr);
  auto pk     = reinterpret_cast<uint8_t*>(arg->pk_addr) + req * MLDSA_PK_BYTES;
  auto sk     = reinterpret_cast<uint8_t*>(arg->sk_addr) + req * MLDSA_SK_BYTES;
  auto sig    = reinterpret_cast<uint8_t*>(arg->sig_addr) + req * MLDSA_SIG_BYTES;
  auto status = reinterpret_cast<int32_t*>(arg->status_addr) + req * MLDSA_ST_COUNT;
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr) + req * MLDSA_CY_COUNT;
  auto arena  = reinterpret_cast<uint32_t*>(arg->arena_addr) + req * MLDSA_AR_COUNT;

  uint32_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

  // The arena is not freed in LIFO order, so it is reset between operations
  // rather than unwound; the peak across all three is what gets reported.
  uint64_t t0 = vx_rdcycle();
  mld_arena_reset();
  status[MLDSA_ST_KEYPAIR] = mldsa_keypair_internal(pk, sk, seed);

  uint64_t t1 = vx_rdcycle();
  mld_arena_reset();
  status[MLDSA_ST_SIGN] = mldsa_signature_internal(sig, msg, MLDSA_MSG_BYTES,
                                                   nullptr, 0, rnd, sk, 0);

  uint64_t t2 = vx_rdcycle();
  mld_arena_reset();
  status[MLDSA_ST_VERIFY] = mldsa_verify_internal(sig, msg, MLDSA_MSG_BYTES,
                                                  nullptr, 0, pk, 0);
  uint64_t t3 = vx_rdcycle();

  cycles[MLDSA_CY_KEYPAIR] = t1 - t0;
  cycles[MLDSA_CY_SIGN]    = t2 - t1;
  cycles[MLDSA_CY_VERIFY]  = t3 - t2;

  const uint32_t h = (uint32_t)vx_hart_id();
  arena[MLDSA_AR_PEAK]       = mld_arena_peak[h];
  arena[MLDSA_AR_FAIL]       = mld_arena_fail[h];
  arena[MLDSA_AR_STACK_PEAK] = pqc_stack_watermark(sp0);
  arena[MLDSA_AR_STACK_SPAN] = span;
}
