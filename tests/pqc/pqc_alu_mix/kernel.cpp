#include <vx_spawn2.h>
#include <pqc/vx_ksg25.h>
#include <pqc/vx_kround25.h>
#include <pqc/vx_ntt.h>
#include "common.h"

#if !defined(VX_CFG_EXT_PQC_ENABLE) || !defined(VX_CFG_EXT_KSG25_ENABLE) \
    || !defined(VX_CFG_EXT_KROUND25_ENABLE)
#error "pqc_alu_mix requires PQC, KSG25 and KROUND25 enabled together"
#endif
static_assert(VX_CFG_XLEN == 32 && VX_CFG_NUM_WARPS == MIX_WARPS
              && VX_CFG_NUM_THREADS == MIX_LANES && VX_CFG_NUM_ALU_LANES == MIX_LANES,
              "pqc_alu_mix requires RV32 W8T32 with 32 ALU lanes");

__kernel void kernel_main(mix_arg_t* __UNIFORM__ arg) {
  const uint32_t index = threadIdx.x;
  const uint32_t role = (index / MIX_LANES) % 4;
  uint64_t state = reinterpret_cast<const uint64_t*>(arg->input_addr)[index];
  const int16_t zeta = int16_t(state >> 32);
  auto output = reinterpret_cast<uint64_t*>(arg->output_addr);

#pragma clang loop unroll(disable)
  for (uint32_t step = 0; step < MIX_STEPS; ++step) {
    // A single CTA keeps all four PE workloads resident and starts each burst together.
    __syncthreads();
    const uint32_t lo = uint32_t(state), hi = uint32_t(state >> 32);
    if (role == 0) {
      const uint32_t tl = vx_ktheta_l_sg25(lo, hi), th = vx_ktheta_h_sg25(lo, hi);
      const uint32_t rl = vx_krhopi_l_sg25(tl, th), rh = vx_krhopi_h_sg25(tl, th);
      const uint32_t cl = vx_kchii_l_sg25(rl, 0), ch = vx_kchii_h_sg25(rh, 0);
      state = (uint64_t(ch) << 32) | cl;
    } else if (role == 1) {
      const uint32_t rl = vx_kround_l_sg25(lo, hi, 0);
      const uint32_t rh = vx_kround_h_sg25(lo, hi, 0);
      state = (uint64_t(rh) << 32) | rl;
    } else if (role == 2) {
      state = uint32_t(int32_t(vx_nttmul_k(int16_t(lo), zeta)));
    } else {
      const int16_t ct = vx_nttbf_ct_k_xor16(int16_t(lo), zeta);
      state = uint32_t(int32_t(vx_nttbf_gs_k_xor1(ct, zeta)));
    }
    output[step * MIX_THREADS + index] = state;
  }
}
