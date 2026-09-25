#ifndef _MLKEM_COOP_ARITH_H_
#define _MLKEM_COOP_ARITH_H_

#include <vx_pqc.h>

template<bool UseNTTMUL>
static inline void mlk_mulcache_w32(int16_t* x, const int16_t* a, unsigned lane) {
  for (unsigned i = lane; i < MLKEM_N / 4; i += 32) {
    const int16_t zeta = mlk_zetas[64 + i];
    const int16_t neg_zeta = (int16_t)-zeta;
    x[2 * i] = UseNTTMUL ? vx_nttmul_k(a[4 * i + 1], zeta)
                        : mlk_fqmul(a[4 * i + 1], zeta);
    x[2 * i + 1] = UseNTTMUL ? vx_nttmul_k(a[4 * i + 3], neg_zeta)
                            : mlk_fqmul(a[4 * i + 3], neg_zeta);
  }
}

template<bool UseNTTMUL>
static inline void mlk_basemul_w32(int16_t* r, const int16_t* a,
                                  const int16_t* b, const int16_t* cache,
                                  unsigned lane) {
  for (unsigned i = lane; i < MLKEM_N / 2; i += 32) {
    int32_t t0 = 0;
    int32_t t1 = 0;
    for (unsigned k = 0; k < MLKEM_K; ++k) {
      const unsigned offset = k * MLKEM_N + 2 * i;
      const int32_t a0 = a[offset];
      const int32_t a1 = a[offset + 1];
      const int32_t b0 = b[offset];
      const int32_t b1 = b[offset + 1];
      t0 += a1 * cache[k * (MLKEM_N / 2) + i];
      t0 += a0 * b0;
      t1 += a0 * b1;
      t1 += a1 * b0;
    }
    if (UseNTTMUL) {
      const int16_t lo0 = (int16_t)t0;
      const int16_t lo1 = (int16_t)t1;
      // Montgomery's correction depends only on low 16 bits; retain the high quotient.
      r[2 * i] = (int16_t)(vx_nttmul_k(lo0, 1) + ((t0 - (int32_t)lo0) >> 16));
      r[2 * i + 1] = (int16_t)(vx_nttmul_k(lo1, 1) + ((t1 - (int32_t)lo1) >> 16));
    } else {
      r[2 * i] = mlk_montgomery_reduce(t0);
      r[2 * i + 1] = mlk_montgomery_reduce(t1);
    }
  }
}

static inline void mlk_reduce_w32(int16_t* p, unsigned lane) {
  for (unsigned i = lane; i < MLKEM_N / 2; i += 32) {
    p[2 * i] = mlk_scalar_signed_to_unsigned_q(mlk_barrett_reduce(p[2 * i]));
    p[2 * i + 1] = mlk_scalar_signed_to_unsigned_q(mlk_barrett_reduce(p[2 * i + 1]));
  }
}

#endif
