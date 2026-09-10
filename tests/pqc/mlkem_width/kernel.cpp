// ML-KEM-768 round trip used by the SIMT-width and SG25 tests. The default and
// pointer backends spread an x4 Keccak batch over the launch block's lanes.
// SG25 runs the surrounding KEM on lane 0 and activates the warp inside each
// FIPS-202 call, where one state is distributed across 25 lanes.

#include <vx_spawn2.h>
#include <vx_intrinsics.h>

#if defined(PQC_KECCAK_SG25)
static_assert(VX_CFG_XLEN == 32, "SG25 requires RV32");
static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "SG25 requires a complete 32-lane ALU vector");
#endif

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
#if !defined(PQC_KECCAK_SG25)
#include "src/fips202/fips202.c"
#include "src/fips202/fips202x4.c"
#include "src/fips202/keccakf1600.c"
#endif
#include "mlkem_native.h"
}

#include "common.h"

static inline uint32_t read_sp() {
  uint32_t v;
  asm volatile("mv %0, sp" : "=r"(v));
  return v;
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
#if defined(PQC_KECCAK_SG25)
  vx_tmc_one();
#endif
  const unsigned req = blockIdx.x;
  if (req >= arg->requests)
    return;
  auto s      = reinterpret_cast<uint8_t*>(arg->scratch_addr) + req * P_SCRATCH_LEN;
  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr) + req * MLKW_COUNT;
  auto status = reinterpret_cast<int32_t*>(arg->status_addr)  + req * 3;
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr) + req * 3;
  auto stack  = reinterpret_cast<uint32_t*>(arg->stack_addr)  + req * 4;
  const unsigned wid = (unsigned)vx_warp_id() & (MLKW_MAX_WARPS - 1);

#if !defined(PQC_KECCAK_SG25)
  // Same value from every lane, so the shared location settles on it.
  mlkw_lanes = arg->lanes;
#endif
  for (int i = 0; i < MLKW_COUNT; ++i)
    mlkw_counts[wid][i] = 0;

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
  mlkw_counts[wid][MLKW_X1_KP] = mlkw_counts[wid][MLKW_KECCAK_X1];
  mlkw_counts[wid][MLKW_X4_KP] = mlkw_counts[wid][MLKW_KECCAK_X4];
  mlk_arena_reset();
  status[1] = mlkem_enc_derand(s + P_OFF_CT, s + P_OFF_SS_ENC, s + P_OFF_PK,
                               s + P_OFF_COINS_ENC);
  uint64_t t2 = vx_rdcycle();
  mlkw_counts[wid][MLKW_X1_ENC] = mlkw_counts[wid][MLKW_KECCAK_X1];
  mlkw_counts[wid][MLKW_X4_ENC] = mlkw_counts[wid][MLKW_KECCAK_X4];
  mlk_arena_reset();
  status[2] = mlkem_dec(s + P_OFF_SS_DEC, s + P_OFF_CT, s + P_OFF_SK);
  uint64_t t3 = vx_rdcycle();
  mlkw_counts[wid][MLKW_X1_DEC] = mlkw_counts[wid][MLKW_KECCAK_X1];
  mlkw_counts[wid][MLKW_X4_DEC] = mlkw_counts[wid][MLKW_KECCAK_X4];

  cycles[0] = t1 - t0;
  cycles[1] = t2 - t1;
  cycles[2] = t3 - t2;

  // Low-water mark: the first still-painted word walking up from the floor.
  uint32_t low = paint_hi;
  for (uint32_t a = floor_addr; a < paint_hi; a += 4) {
    if (*reinterpret_cast<volatile uint32_t*>(a) != MLKW_PAINT) { low = a; break; }
  }
  if (threadIdx.x == 0) {
    stack[0] = sp0 - low;              // peak depth below entry sp
    stack[1] = sp0 - floor_addr;       // paintable span; equality means the
                                       // slab is full and the number is a floor
    const uint32_t h = mlk_arena_index();
    stack[2] = mlk_arena_peak[h];   // arena index comes from
    stack[3] = mlk_arena_fail[h];   // tests/pqc/mlkem/mlk_vortex_alloc.h
  }

  for (int i = 0; i < MLKW_COUNT; ++i)
    counts[i] = mlkw_counts[wid][i];
}
