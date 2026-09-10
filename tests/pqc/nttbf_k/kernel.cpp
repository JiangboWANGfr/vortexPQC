#include <vx_spawn2.h>
#include <vx_pqc.h>

#include "common.h"

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const uint32_t lane = threadIdx.x;
  const uint32_t span = arg->vectors * NTTBF_K_LANES;
  const auto values = reinterpret_cast<const int16_t*>(arg->values_addr);
  const auto zetas = reinterpret_cast<const int16_t*>(arg->zetas_addr);
  auto output = reinterpret_cast<int32_t*>(arg->output_addr);

  for (uint32_t vector = 0; vector < arg->vectors; ++vector) {
    const uint32_t index = vector * NTTBF_K_LANES + lane;
    const int16_t value = values[index];
    const int16_t zeta = zetas[index];
    output[0 * span + index] = vx_nttbf_ct_k_xor1(value, zeta);
    output[1 * span + index] = vx_nttbf_ct_k_xor2(value, zeta);
    output[2 * span + index] = vx_nttbf_ct_k_xor4(value, zeta);
    output[3 * span + index] = vx_nttbf_ct_k_xor8(value, zeta);
    output[4 * span + index] = vx_nttbf_ct_k_xor16(value, zeta);
    output[5 * span + index] = vx_nttbf_gs_k_xor1(value, zeta);
    output[6 * span + index] = vx_nttbf_gs_k_xor2(value, zeta);
    output[7 * span + index] = vx_nttbf_gs_k_xor4(value, zeta);
    output[8 * span + index] = vx_nttbf_gs_k_xor8(value, zeta);
    output[9 * span + index] = vx_nttbf_gs_k_xor16(value, zeta);
  }
}
