#include <vx_spawn2.h>
#include <vx_intrinsics.h>

extern "C" {
#include "src/common.h"
#include "src/compress.c"
#include "src/debug.c"
#include "src/poly.c"
#include "src/poly_k.c"
#include "src/sampling.c"
#include "src/verify.c"
#include "src/fips202/fips202.c"
#include "src/fips202/fips202x4.c"
#include "src/fips202/keccakf1600.c"
}

#include "common.h"
#include "mlkem_coop_arith.h"

static mlk_polyvec input_a[ARITH_MAX_REQUESTS];
static mlk_polyvec input_b[ARITH_MAX_REQUESTS];
static mlk_polyvec_mulcache input_cache[ARITH_MAX_REQUESTS];
static mlk_poly work[ARITH_MAX_REQUESTS];
struct cache_output_t {
  mlk_poly_mulcache value;
  int16_t guard[ARITH_N / 2];
};
static cache_output_t cache_work[ARITH_MAX_REQUESTS];

static __attribute__((noinline)) int16_t coefficient(unsigned index, unsigned req, unsigned sample) {
  static const int16_t edges[] = {
      INT16_MIN, INT16_MIN + 1, -6658, -3330, -3329, -3328, -1665, -1664,
      -1, 0, 1, 1664, 1665, 3328, 3329, 3330, 6658, INT16_MAX - 1, INT16_MAX};
  if (sample == 1 || sample == 5) {
    return INT16_MAX;
  }
  if (sample == 2 || sample == 6) {
    return INT16_MIN;
  }
  if (sample == 3) {
    return (index & 1u) ? INT16_MAX : INT16_MIN;
  }
  if (sample == 4) {
    return edges[(index + req) % (sizeof(edges) / sizeof(edges[0]))];
  }
  return (int16_t)((int)((index * 3121u + req * 977u + sample * 193u) & 65535u)
                   - 32768);
}

static __attribute__((noinline)) void scalar(unsigned operation, unsigned req) {
  if (operation == ARITH_MULCACHE) {
    mlk_poly_mulcache_compute_c(&cache_work[req].value, &input_b[req].vec[0]);
  } else if (operation == ARITH_BASEMUL) {
    MLK_ADD_PARAM_SET(mlk_polyvec_basemul_acc_montgomery_cached_c)(
        &work[req], &input_a[req], &input_b[req], &input_cache[req]);
  } else {
    mlk_poly_reduce_c(&work[req]);
  }
}

template <bool UseNTTMUL>
static __attribute__((noinline)) void cooperative(unsigned operation, unsigned req,
                                                 unsigned lane) {
  if (operation == ARITH_MULCACHE) {
    mlk_mulcache_w32<UseNTTMUL>(cache_work[req].value.coeffs,
                               input_b[req].vec[0].coeffs, lane);
  } else if (operation == ARITH_BASEMUL) {
    mlk_basemul_w32<UseNTTMUL>(work[req].coeffs, input_a[req].vec[0].coeffs,
                              input_b[req].vec[0].coeffs,
                              input_cache[req].vec[0].coeffs, lane);
  } else {
    mlk_reduce_w32(work[req].coeffs, lane);
  }
}

// Keep setup below the compiler's block limit so lane branches receive split/join.
static __attribute__((noinline)) void initialize(const kernel_arg_t* arg, unsigned req,
                                                unsigned lane) {
  const unsigned lanes = arg->mode == ARITH_SCALAR ? 1 : 32;
#pragma clang loop unroll(disable)
  for (unsigned k = 0; k < MLKEM_K; ++k) {
    for (unsigned i = lane; i < ARITH_N; i += lanes) {
      const unsigned index = k * ARITH_N + i;
      int16_t a = (int16_t)((index * 2531u + req * 389u) & 4095u);
      if (arg->sample == 1 || arg->sample == 2 || arg->sample == 5 || arg->sample == 6) {
        a = 4095;
      } else if (arg->sample == 3) {
        a = (i & 1u) ? 4095 : 0;
      } else if (arg->sample == 4) {
        a = k == ((i / 2 + req) % MLKEM_K) ? (i % 3 == 0 ? 1 : 3328) : 0;
      }
      input_a[req].vec[k].coeffs[i] = a;
      input_b[req].vec[k].coeffs[i] = coefficient(index, req, arg->sample);
    }
  }
  __syncthreads();
#pragma clang loop unroll(disable)
  for (unsigned k = 0; k < MLKEM_K; ++k) {
    for (unsigned i = lane; i < ARITH_N / 2; i += lanes) {
      const int16_t zeta = (i & 1u) ? (int16_t)-mlk_zetas[64 + i / 2]
                                   : mlk_zetas[64 + i / 2];
      input_cache[req].vec[k].coeffs[i] = arg->sample >= 5
          ? coefficient(k * ARITH_N / 2 + i, req, arg->sample)
          : mlk_fqmul(input_b[req].vec[k].coeffs[2 * i + 1], zeta);
    }
  }
  for (unsigned i = lane; i < ARITH_N; i += lanes) {
    work[req].coeffs[i] = arg->operation == ARITH_REDUCE
        ? input_b[req].vec[0].coeffs[i] : 0x5a5a;
  }
  if (arg->operation == ARITH_MULCACHE) {
    for (unsigned i = lane; i < ARITH_N / 2; i += lanes) {
      cache_work[req].value.coeffs[i] = 0x5a5a;
      cache_work[req].guard[i] = 0x5a5a;
    }
  }
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned req = blockIdx.x;
  const unsigned lane = threadIdx.x;
  auto output = reinterpret_cast<int16_t*>(arg->output_addr);
  auto p = work[req].coeffs;
  if (arg->exhaustive) {
    for (unsigned batch = req; batch < ARITH_EXHAUSTIVE_COUNT / ARITH_N;
         batch += arg->requests) {
      for (unsigned i = lane; i < ARITH_N; i += 32) {
        p[i] = (int16_t)((int)(batch * ARITH_N + i) - 32768);
      }
      __syncthreads();
      mlk_reduce_w32(p, lane);
      __syncthreads();
      for (unsigned i = lane; i < ARITH_N; i += 32) {
        output[batch * ARITH_N + i] = p[i];
      }
      __syncthreads();
    }
    return;
  }

  const unsigned lanes = arg->mode == ARITH_SCALAR ? 1 : 32;
  initialize(arg, req, lane);
  // All resident warps finish setup before any timed operation begins.
  vx_barrier(1u << 8, arg->requests);
  const uint64_t start = vx_rdcycle_sync();
  if (arg->mode == ARITH_SCALAR) {
    scalar(arg->operation, req);
  } else if (arg->mode == ARITH_W32_C) {
    cooperative<false>(arg->operation, req, lane);
  } else {
    cooperative<true>(arg->operation, req, lane);
  }
  __syncthreads();
  const uint64_t end = vx_rdcycle_sync();
  vx_barrier(1u << 8, arg->requests);

  const unsigned slot = (((arg->operation * ARITH_CASES + arg->sample)
                         * ARITH_MODE_COUNT + arg->mode) * arg->requests) + req;
  for (unsigned i = lane; i < ARITH_N; i += lanes) {
    output[slot * ARITH_N + i] = arg->operation == ARITH_MULCACHE
        ? (i < ARITH_N / 2 ? cache_work[req].value.coeffs[i]
                          : cache_work[req].guard[i - ARITH_N / 2])
        : p[i];
  }
  if (lane == 0) {
    auto result = reinterpret_cast<arith_result_t*>(arg->results_addr) + slot;
    result->start = start;
    result->end = end;
  }
}
