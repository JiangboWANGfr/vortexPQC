// The library is a single translation unit and is pulled in here rather than
// from a second .cpp: a file-scope arena in a header reaching two TUs would
// give two arenas, the library allocating from one and the kernel reporting the
// other -- peak reads zero and the KAT still passes. That failure has already
// happened once in this test suite.
extern "C" {
#include "mlkem_native.c"
}

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"
#include "pqc_stack.h"

// ML-KEM-768 round trip, one independent request per CTA, one lane per request.
//
// One lane per request on purpose. This test builds the library pristine, so
// its x4 Keccak hook falls back to four serial permutations and there is no
// lane-cooperative work to hand a second lane; extra lanes would redundantly
// recompute the whole KEM over the same output slice and multiply the arena for
// nothing. The lane axis is tests/pqc/mlkem_width's job. This one is the
// request axis, and the software throughput baseline every later ISA throughput
// number has to divide into.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned req = blockIdx.x;
  // block_dim is 1 and grid_dim is exactly `requests`, so both of these only
  // fire if a harness rounds the launch up -- in which case the extra work
  // would write past the end of the output slices.
  if (threadIdx.x != 0 || req >= arg->requests)
    return;

  auto coins_kp  = reinterpret_cast<const uint8_t*>(arg->coins_kp_addr);
  auto coins_enc = reinterpret_cast<const uint8_t*>(arg->coins_enc_addr);
  auto pk        = reinterpret_cast<uint8_t*>(arg->pk_addr)     + req * MLKEM_PK_BYTES;
  auto sk        = reinterpret_cast<uint8_t*>(arg->sk_addr)     + req * MLKEM_SK_BYTES;
  auto ct        = reinterpret_cast<uint8_t*>(arg->ct_addr)     + req * MLKEM_CT_BYTES;
  auto ss_enc    = reinterpret_cast<uint8_t*>(arg->ss_enc_addr) + req * MLKEM_SS_BYTES;
  auto ss_dec    = reinterpret_cast<uint8_t*>(arg->ss_dec_addr) + req * MLKEM_SS_BYTES;
  auto status    = reinterpret_cast<int32_t*>(arg->status_addr) + req * MLKEM_ST_COUNT;
  auto cycles    = reinterpret_cast<uint64_t*>(arg->cycles_addr)+ req * MLKEM_CY_COUNT;
  auto probe     = reinterpret_cast<uint32_t*>(arg->probe_addr) + req * MLKEM_PR_COUNT;

  uintptr_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

  uint64_t t0 = vx_rdcycle();
  mlk_arena_reset();
  status[MLKEM_ST_KEYPAIR] = mlkem_keypair_derand(pk, sk, coins_kp);
  uint64_t t1 = vx_rdcycle();
  mlk_arena_reset();
  status[MLKEM_ST_ENCAPS]  = mlkem_enc_derand(ct, ss_enc, pk, coins_enc);
  uint64_t t2 = vx_rdcycle();
  mlk_arena_reset();
  status[MLKEM_ST_DECAPS]  = mlkem_dec(ss_dec, ct, sk);
  uint64_t t3 = vx_rdcycle();

  cycles[MLKEM_CY_KEYPAIR] = t1 - t0;
  cycles[MLKEM_CY_ENCAPS]  = t2 - t1;
  cycles[MLKEM_CY_DECAPS]  = t3 - t2;

  probe[MLKEM_PR_STACK_PEAK] = pqc_stack_watermark(sp0);
  probe[MLKEM_PR_STACK_SPAN] = span;
  const uint32_t h = (uint32_t)vx_hart_id();
  probe[MLKEM_PR_ARENA_PEAK] = mlk_arena_peak[h];
  probe[MLKEM_PR_ARENA_FAIL] = mlk_arena_fail[h];
}
