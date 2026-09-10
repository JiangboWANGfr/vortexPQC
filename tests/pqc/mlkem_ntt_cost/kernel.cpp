#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include <vx_pqc.h>

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

static mlk_poly work;
static mlk_poly reference;

template <unsigned Mode>
static inline int16_t fqmul(int16_t a, int16_t b) {
  if constexpr (Mode == 1) {
    int32_t result;
    // Both operands remain live, including twiddle loads, and feed the result.
    asm volatile ("xor %0, %1, %2\n\txor %0, %0, %2"
                  : "=&r"(result) : "r"((int32_t)a), "r"((int32_t)b));
    return (int16_t)result;
  } else if constexpr (Mode == 2) {
    return vx_nttmul_k(a, b);
  } else {
    return mlk_fqmul(a, b);
  }
}

template <unsigned Mode>
static __attribute__((noinline)) void ntt(mlk_poly* p) {
  int16_t* r = p->coeffs;
  // Keep the same loop structure when the multiply body changes size.
#pragma clang loop unroll(disable)
  for (unsigned layer = 1; layer <= 7; ++layer) {
    unsigned k = 1u << (layer - 1);
    const unsigned len = MLKEM_N >> layer;
#pragma clang loop unroll(disable)
    for (unsigned start = 0; start < MLKEM_N; start += 2 * len) {
      const int16_t zeta = mlk_zetas[k++];
#pragma clang loop unroll(disable)
      for (unsigned j = start; j < start + len; ++j) {
        const int16_t t = fqmul<Mode>(r[j + len], zeta);
        r[j + len] = (int16_t)(r[j] - t);
        r[j] = (int16_t)(r[j] + t);
      }
    }
  }
}

template <unsigned Mode>
static __attribute__((noinline)) void intt(mlk_poly* p) {
  int16_t* r = p->coeffs;
#pragma clang loop unroll(disable)
  for (unsigned j = 0; j < MLKEM_N; ++j) {
    r[j] = fqmul<Mode>(r[j], 1441);
  }
#pragma clang loop unroll(disable)
  for (unsigned layer = 7; layer > 0; --layer) {
    const unsigned len = MLKEM_N >> layer;
    unsigned k = (1u << layer) - 1;
#pragma clang loop unroll(disable)
    for (unsigned start = 0; start < MLKEM_N; start += 2 * len) {
      const int16_t zeta = mlk_zetas[k--];
#pragma clang loop unroll(disable)
      for (unsigned j = start; j < start + len; ++j) {
        const int16_t t = r[j];
        r[j] = mlk_barrett_reduce((int16_t)(t + r[j + len]));
        r[j + len] = fqmul<Mode>((int16_t)(r[j + len] - t), zeta);
      }
    }
  }
}

static void initialize(mlk_poly* p, unsigned seed) {
  for (unsigned j = 0; j < MLKEM_N; ++j) {
    p->coeffs[j] = (int16_t)((int)((j * 3121u + seed * 17u)
                          % (2u * MLKEM_Q - 1u)) - (MLKEM_Q - 1));
  }
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto result = reinterpret_cast<cost_result_t*>(arg->result_addr);
  *result = {};
  using transform_t = void (*)(mlk_poly*);
  const transform_t transforms[COST_COUNT] = {
    ntt<0>, ntt<1>, ntt<2>, intt<0>, intt<1>, intt<2>
  };

  for (unsigned inverse = 0; inverse < 2; ++inverse) {
    for (unsigned iteration = 0; iteration <= arg->iterations; ++iteration) {
      initialize(&reference, iteration);
      if (inverse) {
        mlk_poly_invntt_tomont_c(&reference);
      } else {
        mlk_poly_ntt_c(&reference);
      }

      for (unsigned order = 0; order < 3; ++order) {
        const unsigned mode = (order + iteration) % 3;
        const unsigned index = 3 * inverse + mode;
        const transform_t transform = transforms[index];
        initialize(&work, iteration);
        vx_fence();
        const uint32_t i0 = csr_read(VX_CSR_MINSTRET);
        const uint64_t t0 = vx_rdcycle_sync();
        transform(&work);
        vx_fence();
        const uint64_t t1 = vx_rdcycle_sync();
        const uint32_t i1 = csr_read(VX_CSR_MINSTRET);

        uint32_t checksum = 2166136261u;
        for (unsigned j = 0; j < MLKEM_N; ++j) {
          checksum = (checksum ^ (uint16_t)work.coeffs[j]) * 16777619u;
          if (mode != 1 && work.coeffs[j] != reference.coeffs[j]) {
            ++result->mismatches[index];
          }
        }
        result->checksums[index] = checksum;
        // The first triplet warms code and data before any arm contributes.
        if (iteration) {
          result->cycles[index] += t1 - t0;
          result->instructions[index] += (uint32_t)(i1 - i0);
        }
      }
    }
  }
  result->completed = arg->iterations;
}
