// ML-KEM-768 round trip with the x4 Keccak batch spread over W warp lanes.
//
// W is the launch's block size, not a compile-time constant, so every arm of
// the sweep is the same binary and the same instruction stream; the only thing
// that changes is how many lanes the batch lands on. Every active lane runs
// the whole KEM on the same data, so the non-Keccak 28% is perfectly
// convergent and costs what it costs on one lane; the Keccak batch is the only
// place the lanes do different work.

#include <vx_spawn2.h>
#include <vx_intrinsics.h>

extern "C" {
#include "src/common.h"
#include "src/compress.c"
#include "src/debug.c"
#include "src/indcpa.c"
#include "src/kem.c"
#include "src/poly.c"
#include "src/poly_k.c"
#include "src/sampling.c"
#include "src/verify.c"
#include "src/fips202/fips202.c"
#include "src/fips202/fips202x4.c"
#include "src/fips202/keccakf1600.c"
#include "mlkem_native.h"
}

#include "common.h"

static inline uint32_t read_sp() {
  uint32_t v;
  asm volatile("mv %0, sp" : "=r"(v));
  return v;
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto s      = reinterpret_cast<uint8_t*>(arg->scratch_addr);
  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr);
  auto status = reinterpret_cast<int32_t*>(arg->status_addr);
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);
  auto stack  = reinterpret_cast<uint32_t*>(arg->stack_addr);

  // Same value from every lane, so the shared location settles on it.
  mlkw_lanes = arg->lanes;
  for (int i = 0; i < MLKW_COUNT; ++i)
    mlkw_counts[i] = 0;

  // Paint this lane's 8 KB stack slab below the current frame. The slab is
  // 8 KB aligned by construction (vx_start.S: sp = BASE - hartid << 13), so
  // masking the live sp finds its floor. Painting is what turns a silent
  // overflow into a measured number -- at one lane there is no neighbour to
  // corrupt, so nothing ever reports it.
  const uint32_t sp0 = read_sp();
#ifndef PQC_PAINT_SLABS
#define PQC_PAINT_SLABS 1
#endif
  const uint32_t floor_addr = (sp0 & ~8191u) - (PQC_PAINT_SLABS - 1) * 8192u;
  const uint32_t paint_hi = sp0 - 128;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4)
    *reinterpret_cast<volatile uint32_t*>(a) = MLKW_PAINT;

  vx_fence();

  mlk_arena_reset();
  uint64_t t0 = vx_rdcycle();
  status[0] = mlkem_keypair_derand(s + P_OFF_PK, s + P_OFF_SK, s + P_OFF_COINS_KP);
  uint64_t t1 = vx_rdcycle();
  mlkw_counts[MLKW_X1_KP] = mlkw_counts[MLKW_KECCAK_X1];
  mlkw_counts[MLKW_X4_KP] = mlkw_counts[MLKW_KECCAK_X4];
  mlk_arena_reset();
  status[1] = mlkem_enc_derand(s + P_OFF_CT, s + P_OFF_SS_ENC, s + P_OFF_PK,
                               s + P_OFF_COINS_ENC);
  uint64_t t2 = vx_rdcycle();
  mlkw_counts[MLKW_X1_ENC] = mlkw_counts[MLKW_KECCAK_X1];
  mlkw_counts[MLKW_X4_ENC] = mlkw_counts[MLKW_KECCAK_X4];
  mlk_arena_reset();
  status[2] = mlkem_dec(s + P_OFF_SS_DEC, s + P_OFF_CT, s + P_OFF_SK);
  uint64_t t3 = vx_rdcycle();
  mlkw_counts[MLKW_X1_DEC] = mlkw_counts[MLKW_KECCAK_X1];
  mlkw_counts[MLKW_X4_DEC] = mlkw_counts[MLKW_KECCAK_X4];

  cycles[0] = t1 - t0;
  cycles[1] = t2 - t1;
  cycles[2] = t3 - t2;

  // Low-water mark: the first still-painted word walking up from the floor.
  uint32_t low = paint_hi;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4) {
    if (*reinterpret_cast<volatile uint32_t*>(a) != MLKW_PAINT) { low = a; break; }
  }
  if (threadIdx.x == 0 && blockIdx.x == 0) {
    stack[0] = sp0 - low;              // peak depth below entry sp
    stack[1] = sp0 - floor_addr;       // paintable span; equality means the
                                       // slab is full and the number is a floor
    const uint32_t h = (uint32_t)vx_hart_id();
    stack[2] = mlk_arena_peak[h];   // shared per-hart arena, see
    stack[3] = mlk_arena_fail[h];   // tests/pqc/mlkem/mlk_vortex_alloc.h
  }

  for (int i = 0; i < MLKW_COUNT; ++i)
    counts[i] = mlkw_counts[i];
}
