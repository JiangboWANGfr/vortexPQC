// gen_matrix and the encaps noise stage at an arbitrary SIMT batch width.
//
// The library's batch width is nailed to four -- MLK_KECCAK_WAY is a constant
// in keccakf1600.h, mlk_poly_rej_uniform_x4 takes four polynomial pointers,
// mlk_gen_matrix steps i += 4, and mlk_prf_eta1_x4 unrolls four seeds by hand.
// None of that is behind the native-backend hook, so "generalise the x4 API to
// xN" is a change to library source, not a backend. What this test does
// instead is put the xN loop where it belongs on a SIMT machine: above the
// library, one independent XOF stream per lane, using the library's own
// unbatched SHAKE-128 and rejection sampler for the per-stream work. Each
// stream is lane-private, so unlike the x4 API -- whose four states share one
// interleaved 4x25 array and therefore need a scatter/gather around every
// permutation -- this pays nothing to be wide.

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

// sampling.c #undefs this at the end for single-compilation-unit builds, so
// the same expression is restated here rather than reaching into the library.
#define GX_NBLOCKS \
  ((12 * MLKEM_N / 8 * ((uint32_t)1 << 12) / MLKEM_Q + SHAKE128_RATE) / SHAKE128_RATE)

static inline uint32_t read_sp() {
  uint32_t v; asm volatile("mv %0, sp" : "=r"(v)); return v;
}

// Number of SHAKE-128 blocks each stream actually consumed, so a width sweep
// can be read as slots rather than guessed at.
static uint32_t gx_blocks;

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const unsigned blk = blockIdx.x;
  auto out    = reinterpret_cast<int16_t*>(arg->poly_addr) + blk * GX_ENTRIES * 256;
  auto nout   = reinterpret_cast<int16_t*>(arg->noise_addr) + blk * GX_NOISE * 256;
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);
  auto sums   = reinterpret_cast<uint32_t*>(arg->sum_addr);

  const unsigned tid = (unsigned)vx_thread_id();
  const unsigned W   = arg->lanes;

  const uint32_t sp0 = read_sp();
  const uint32_t floor_addr = (sp0 & ~8191u);
  const uint32_t paint_hi = sp0 - 128;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4)
    *reinterpret_cast<volatile uint32_t*>(a) = GX_PAINT;
  vx_fence();

  uint8_t seed[MLKEM_SYMBYTES];
  for (unsigned i = 0; i < MLKEM_SYMBYTES; ++i) seed[i] = (uint8_t)(i * 7 + 1);
  seed[0] = (uint8_t)(seed[0] ^ blk);   // each CTA is a different message

  gx_blocks = 0;

  // ---- gen_matrix, 9 independent streams over W lanes -------------------
  uint64_t t0 = vx_rdcycle();
  for (unsigned e = tid; e < GX_ENTRIES; e += W)
  {
    MLK_ALIGN uint8_t buf[GX_NBLOCKS * SHAKE128_RATE];
    uint8_t s[MLKEM_SYMBYTES + 2];
    mlk_shake128ctx st;
    unsigned ctr, nb = GX_NBLOCKS;
    for (unsigned i = 0; i < MLKEM_SYMBYTES; ++i) s[i] = seed[i];
    s[MLKEM_SYMBYTES + 0] = (uint8_t)(e % 3);
    s[MLKEM_SYMBYTES + 1] = (uint8_t)(e / 3);

    mlk_shake128_init(&st);
    mlk_shake128_absorb_once(&st, s, MLKEM_SYMBYTES + 2);
    mlk_shake128_squeezeblocks(buf, GX_NBLOCKS, &st);
    ctr = mlk_rej_uniform_c(out + e * MLKEM_N, MLKEM_N, 0, buf,
                            GX_NBLOCKS * SHAKE128_RATE);
    while (ctr < MLKEM_N)
    {
      mlk_shake128_squeezeblocks(buf, 1, &st);
      ctr = mlk_rej_uniform_c(out + e * MLKEM_N, MLKEM_N, ctr, buf, SHAKE128_RATE);
      nb++;
    }
    mlk_shake128_release(&st);
    gx_blocks += nb;
  }
  uint64_t t1 = vx_rdcycle();

  // ---- encaps noise, 7 independent PRF streams over W lanes -------------
  for (unsigned e = tid; e < GX_NOISE; e += W)
  {
    MLK_ALIGN uint8_t buf[MLKEM_ETA1 * MLKEM_N / 4];
    uint8_t k[MLKEM_SYMBYTES + 1];
    for (unsigned i = 0; i < MLKEM_SYMBYTES; ++i) k[i] = seed[i];
    k[MLKEM_SYMBYTES] = (uint8_t)e;
    mlk_shake256(buf, sizeof(buf), k, sizeof(k));
    mlk_poly_cbd2(reinterpret_cast<mlk_poly*>(nout + e * MLKEM_N), buf);
  }
  uint64_t t2 = vx_rdcycle();

  uint32_t low = paint_hi;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4)
    if (*reinterpret_cast<volatile uint32_t*>(a) != GX_PAINT) { low = a; break; }

  if (tid == 0 && blk == 0) {
    cycles[0] = t1 - t0;
    cycles[1] = t2 - t1;
    cycles[2] = gx_blocks;
    cycles[3] = sp0 - low;
    uint32_t a = 0, b = 0;
    for (unsigned i = 0; i < GX_ENTRIES * MLKEM_N; ++i) a = a * 31u + (uint16_t)out[i];
    for (unsigned i = 0; i < GX_NOISE   * MLKEM_N; ++i) b = b * 31u + (uint16_t)nout[i];
    sums[0] = a; sums[1] = b;
  }
}
