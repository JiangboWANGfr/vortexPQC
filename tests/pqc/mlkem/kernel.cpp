#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"

// ML-KEM-768 round trip, one thread.
//
// The baseline is deliberately single-threaded: it is the denominator every
// later speedup is measured against, and a parallel baseline would fold the
// question "does the extension help?" together with "did we parallelise it
// well?". Threading comes after the profile says where the time goes.
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  // One warp, one lane. The launch is 1x1, so this is belt-and-braces against
  // a harness that rounds a grid up.
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  auto coins_kp  = reinterpret_cast<const uint8_t*>(arg->coins_kp_addr);
  auto coins_enc = reinterpret_cast<const uint8_t*>(arg->coins_enc_addr);
  auto pk        = reinterpret_cast<uint8_t*>(arg->pk_addr);
  auto sk        = reinterpret_cast<uint8_t*>(arg->sk_addr);
  auto ct        = reinterpret_cast<uint8_t*>(arg->ct_addr);
  auto ss_enc    = reinterpret_cast<uint8_t*>(arg->ss_enc_addr);
  auto ss_dec    = reinterpret_cast<uint8_t*>(arg->ss_dec_addr);
  auto status    = reinterpret_cast<int32_t*>(arg->status_addr);
  auto cycles    = reinterpret_cast<uint64_t*>(arg->cycles_addr);

  uint64_t t0 = vx_rdcycle();
  status[MLKEM_ST_KEYPAIR] = mlkem_keypair_derand(pk, sk, coins_kp);
  uint64_t t1 = vx_rdcycle();
  status[MLKEM_ST_ENCAPS]  = mlkem_enc_derand(ct, ss_enc, pk, coins_enc);
  uint64_t t2 = vx_rdcycle();
  status[MLKEM_ST_DECAPS]  = mlkem_dec(ss_dec, ct, sk);
  uint64_t t3 = vx_rdcycle();

  cycles[MLKEM_CY_KEYPAIR] = t1 - t0;
  cycles[MLKEM_CY_ENCAPS]  = t2 - t1;
  cycles[MLKEM_CY_DECAPS]  = t3 - t2;
}
