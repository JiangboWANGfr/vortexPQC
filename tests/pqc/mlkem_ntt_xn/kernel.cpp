#include <vx_spawn2.h>
#include <vx_intrinsics.h>

extern "C" {
#include "src/common.h"
#include "src/compress.c"
#include "src/debug.c"
#include "src/poly.c"
#include "src/verify.c"
#include "src/fips202/fips202.c"
#include "src/fips202/keccakf1600.c"
}

#include "common.h"
#include "mlkem_coop_ntt.h"
#include "pqc_stack.h"

// A shared polynomial per request avoids lane-private stacks and warp aliases.
static mlk_poly ntt_work[NTT_MAX_REQUESTS];

static __attribute__((noinline)) int16_t input(unsigned i, unsigned req, unsigned sample, unsigned inverse) {
  const int high = inverse ? INT16_MAX : MLKEM_Q - 1;
  const int low = inverse ? INT16_MIN : 1 - MLKEM_Q;
  if (sample == 0) {
    return (int16_t)((int)((i * 3121u + req * 977u) % (2u * MLKEM_Q - 1u))
                     - (MLKEM_Q - 1));
  }
  if (sample == 1) {
    return i == req ? low : high;
  }
  if (sample == 2) {
    return i == req ? 0 : ((i & 1u) ? high : low);
  }
  if (sample == 3) {
    return i == req ? 1 : 0;
  }
  return i == req ? high : low;
}

static __attribute__((noinline)) void reference(mlk_poly* p, unsigned inverse) {
  if (inverse) {
    mlk_poly_invntt_tomont_c(p);
  } else {
    mlk_poly_ntt_c(p);
  }
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned req = blockIdx.x;
  const unsigned tid = threadIdx.x;
  const unsigned lanes = arg->reference ? 1 : arg->lanes;
  const unsigned slot = (arg->inverse * NTT_CASES + arg->sample) * arg->requests + req;
  auto out = reinterpret_cast<int16_t*>(arg->poly_addr) + slot * NTT_N;
  auto ref = reinterpret_cast<int16_t*>(arg->ref_addr) + slot * NTT_N;
  auto result = reinterpret_cast<ntt_result_t*>(arg->results_addr) + slot;
  mlk_poly* p = &ntt_work[req];

  uint32_t sp0 = 0;
  uint32_t span = 0;
  if (tid == 0) {
    span = pqc_stack_paint(&sp0);
  }
  for (unsigned i = tid; i < NTT_N; i += lanes) {
    p->coeffs[i] = input(i, req, arg->sample, arg->inverse);
  }
  // All resident warps finish setup before any request starts its measurement.
  vx_barrier(1u << 8, arg->requests);

  const uint64_t start = vx_rdcycle_sync();
  if (arg->reference) {
    reference(p, arg->inverse);
#if defined(PQC_NTT_SMEM32)
  } else if (arg->inverse) {
    mlk_invntt_smem_w32(p->coeffs, reinterpret_cast<int32_t*>(__local_mem()), tid);
  } else {
    mlk_ntt_smem_w32(p->coeffs, reinterpret_cast<int32_t*>(__local_mem()), tid);
#elif defined(PQC_NTT_REG32)
  } else if (arg->inverse) {
    mlk_invntt_w32(p->coeffs, tid);
  } else {
    mlk_ntt_w32(p->coeffs, tid);
#else
  } else if (arg->inverse) {
    mlk_invntt_coop(p->coeffs, lanes, tid);
  } else {
    mlk_ntt_coop(p->coeffs, lanes, tid);
#endif
  }
  __syncthreads();
  const uint64_t end = vx_rdcycle_sync();
  vx_barrier(1u << 8, arg->requests);

  if (tid == 0) {
    uint32_t bad = 0;
    for (unsigned i = 0; i < NTT_N; ++i) {
      if (arg->reference) {
        ref[i] = p->coeffs[i];
        if (out[i] != ref[i]) {
          ++bad;
        }
      } else {
        out[i] = p->coeffs[i];
      }
    }
    const uint32_t peak = pqc_stack_watermark(sp0);
    if (arg->reference) {
      result->ref_start = start;
      result->ref_end = end;
      result->mismatches = bad;
      if (peak > result->stack_peak) {
        result->stack_peak = peak;
      }
      if (span < result->stack_span) {
        result->stack_span = span;
      }
    } else {
      result->coop_start = start;
      result->coop_end = end;
      result->stack_span = span;
      result->stack_peak = peak;
    }
  }
}
