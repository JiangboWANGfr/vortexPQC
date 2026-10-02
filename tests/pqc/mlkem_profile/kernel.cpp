// Primitive call counts and optional cooperative paths for an ML-KEM round trip.
//
// Sources are included per-file rather than through mlkem_native.c so the
// counter array stays reachable; see tests/pqc/mlkem_microbench/kernel.cpp for why
// the monolithic wrapper cannot be used here.
extern "C" {
#include "src/common.h"
#include "src/compress.c"
#include "src/debug.c"
#if defined(PQC_LINEAR_WARP)
#include "src/poly_k.h"
void mlk_profile_polyvec_tomont(mlk_polyvec* r);
void mlk_profile_polyvec_add(mlk_polyvec* r, const mlk_polyvec* b);
void mlk_profile_poly_add(mlk_poly* r, const mlk_poly* b);
void mlk_profile_poly_sub(mlk_poly* r, const mlk_poly* b);
void mlk_profile_poly_frommsg(mlk_poly* r, const uint8_t* msg);
void mlk_profile_poly_tomsg(uint8_t* msg, const mlk_poly* r);
#undef mlk_polyvec_tomont
#define mlk_polyvec_tomont mlk_profile_polyvec_tomont
#undef mlk_polyvec_add
#define mlk_polyvec_add mlk_profile_polyvec_add
#undef mlk_poly_add
#define mlk_poly_add mlk_profile_poly_add
#undef mlk_poly_sub
#define mlk_poly_sub mlk_profile_poly_sub
#undef mlk_poly_frommsg
#define mlk_poly_frommsg mlk_profile_poly_frommsg
#undef mlk_poly_tomsg
#define mlk_poly_tomsg mlk_profile_poly_tomsg
#endif
#include "src/indcpa.c"
#if defined(PQC_LINEAR_WARP)
#undef mlk_polyvec_tomont
#define mlk_polyvec_tomont MLK_NAMESPACE(polyvec_tomont)
#undef mlk_polyvec_add
#define mlk_polyvec_add MLK_NAMESPACE(polyvec_add)
#undef mlk_poly_add
#define mlk_poly_add MLK_NAMESPACE(poly_add)
#undef mlk_poly_sub
#define mlk_poly_sub MLK_NAMESPACE(poly_sub)
#undef mlk_poly_frommsg
#define mlk_poly_frommsg MLK_NAMESPACE(poly_frommsg)
#undef mlk_poly_tomsg
#define mlk_poly_tomsg MLK_NAMESPACE(poly_tomsg)
#endif
#include "src/kem.c"
#include "src/poly.c"
#if defined(PQC_NOISE_WARP)
#include "src/sampling.h"
void mlk_profile_poly_cbd2(mlk_poly* r, const uint8_t* buf);
#if MLKEM_ETA1 == 3
void mlk_profile_poly_cbd3(mlk_poly* r, const uint8_t* buf);
#endif
#undef mlk_poly_cbd2
#define mlk_poly_cbd2 mlk_profile_poly_cbd2
#if MLKEM_ETA1 == 3
#undef mlk_poly_cbd3
#define mlk_poly_cbd3 mlk_profile_poly_cbd3
#endif
#endif
#include "src/poly_k.c"
#if defined(PQC_NOISE_WARP)
#undef mlk_poly_cbd2
#define mlk_poly_cbd2 MLK_NAMESPACE(poly_cbd2)
#if MLKEM_ETA1 == 3
#undef mlk_poly_cbd3
#define mlk_poly_cbd3 MLK_NAMESPACE(poly_cbd3)
#endif
#endif
#include "src/sampling.c"
#include "src/verify.c"
#if !defined(PQC_KECCAK_SG25)
#if defined(PQC_PROFILE_PHASES)
#include "src/fips202/fips202.h"
#include "src/fips202/keccakf1600.h"

// Time vendored FIPS helpers through local aliases so the submodule stays unchanged.
#define MLK_PROFILE_REAL_PERMUTE MLK_NAMESPACE(keccakf1600_permute)

static void mlk_profile_raw_absorb_once(uint64_t*, unsigned, const uint8_t*,
                                        size_t, uint8_t);
static void mlk_profile_raw_squeezeblocks(uint8_t*, size_t, uint64_t*,
                                          unsigned);
static void mlk_profile_raw_squeeze_once(uint8_t*, size_t, uint64_t*,
                                         unsigned);

static void mlk_profile_permute(uint64_t* state) {
  const mlk_phase_scope_t scope = mlk_phase_begin();
  MLK_PROFILE_REAL_PERMUTE(state);
  mlk_phase_end(MLK_PHASE_PERMUTE, scope);
}

static void mlk_profile_absorb(uint64_t* state, unsigned rate,
                               const uint8_t* input, size_t inlen,
                               uint8_t domain) {
  const mlk_phase_scope_t scope = mlk_phase_begin();
  mlk_profile_raw_absorb_once(state, rate, input, inlen, domain);
  mlk_phase_end_excluding_permute(MLK_PHASE_ABSORB, scope);
}

static void mlk_profile_squeezeblocks(uint8_t* output, size_t nblocks,
                                      uint64_t* state, unsigned rate) {
  const mlk_phase_scope_t scope = mlk_phase_begin();
  mlk_profile_raw_squeezeblocks(output, nblocks, state, rate);
  mlk_phase_end_excluding_permute(MLK_PHASE_SQUEEZE, scope);
}

static void mlk_profile_squeeze(uint8_t* output, size_t outlen,
                                uint64_t* state, unsigned rate) {
  const mlk_phase_scope_t scope = mlk_phase_begin();
  mlk_profile_raw_squeeze_once(output, outlen, state, rate);
  mlk_phase_end_excluding_permute(MLK_PHASE_SQUEEZE, scope);
}

#undef mlk_keccakf1600_permute
#define mlk_keccakf1600_permute mlk_profile_permute

#define mlk_keccak_absorb_once mlk_profile_raw_absorb_once
#define mlk_keccak_squeezeblocks mlk_profile_raw_squeezeblocks
#define mlk_keccak_squeeze_once mlk_profile_raw_squeeze_once
#undef mlk_shake128_absorb_once
#undef mlk_shake128_squeezeblocks
#undef mlk_shake128_init
#undef mlk_shake128_release
#undef mlk_shake256
#undef mlk_sha3_256
#undef mlk_sha3_512
#define mlk_shake128_absorb_once mlk_profile_raw_shake128_absorb_once
#define mlk_shake128_squeezeblocks mlk_profile_raw_shake128_squeezeblocks
#define mlk_shake128_init mlk_profile_raw_shake128_init
#define mlk_shake128_release mlk_profile_raw_shake128_release
#define mlk_shake256 mlk_profile_raw_shake256
#define mlk_sha3_256 mlk_profile_raw_sha3_256
#define mlk_sha3_512 mlk_profile_raw_sha3_512
#include "src/fips202/fips202.c"
#undef mlk_keccak_absorb_once
#undef mlk_keccak_squeezeblocks
#undef mlk_keccak_squeeze_once
#undef mlk_keccakf1600_permute
#undef mlk_shake128_absorb_once
#undef mlk_shake128_squeezeblocks
#undef mlk_shake128_init
#undef mlk_shake128_release
#undef mlk_shake256
#undef mlk_sha3_256
#undef mlk_sha3_512
#define mlk_shake128_absorb_once MLK_NAMESPACE(shake128_absorb_once)
#define mlk_shake128_squeezeblocks MLK_NAMESPACE(shake128_squeezeblocks)
#define mlk_shake128_init MLK_NAMESPACE(shake128_init)
#define mlk_shake128_release MLK_NAMESPACE(shake128_release)
#define mlk_shake256 MLK_NAMESPACE(shake256)
#define mlk_sha3_256 MLK_NAMESPACE(sha3_256)
#define mlk_sha3_512 MLK_NAMESPACE(sha3_512)
#define mlk_keccakf1600_permute MLK_PROFILE_REAL_PERMUTE

void mlk_shake128_absorb_once(mlk_shake128ctx* state, const uint8_t* input,
                              size_t inlen) {
  mlk_profile_absorb(state->ctx, SHAKE128_RATE, input, inlen, 0x1f);
}

void mlk_shake128_squeezeblocks(uint8_t* output, size_t nblocks,
                                mlk_shake128ctx* state) {
  mlk_profile_squeezeblocks(output, nblocks, state->ctx, SHAKE128_RATE);
}

void mlk_shake128_init(mlk_shake128ctx* state) {
  (void)state;
}

void mlk_shake128_release(mlk_shake128ctx* state) {
  mlk_zeroize(state, sizeof(*state));
}

void mlk_shake256(uint8_t* output, size_t outlen, const uint8_t* input,
                  size_t inlen) {
  mlk_shake128ctx state;
  mlk_profile_absorb(state.ctx, SHAKE256_RATE, input, inlen, 0x1f);
  mlk_profile_squeeze(output, outlen, state.ctx, SHAKE256_RATE);
  mlk_zeroize(&state, sizeof(state));
}

void mlk_sha3_256(uint8_t* output, const uint8_t* input, size_t inlen) {
  uint64_t state[25];
  mlk_profile_absorb(state, SHA3_256_RATE, input, inlen, 0x06);
  mlk_profile_squeeze(output, SHA3_256_HASHBYTES, state, SHA3_256_RATE);
  mlk_zeroize(state, sizeof(state));
}

void mlk_sha3_512(uint8_t* output, const uint8_t* input, size_t inlen) {
  uint64_t state[25];
  mlk_profile_absorb(state, SHA3_512_RATE, input, inlen, 0x06);
  mlk_profile_squeeze(output, SHA3_512_HASHBYTES, state, SHA3_512_RATE);
  mlk_zeroize(state, sizeof(state));
}
#else
#include "src/fips202/fips202.c"
#include "src/fips202/fips202x4.c"
#endif
#include "src/fips202/keccakf1600.c"
#if defined(PQC_PROFILE_PHASES)
#undef mlk_keccakf1600_permute
#define mlk_keccakf1600_permute MLK_NAMESPACE(keccakf1600_permute)
#undef MLK_PROFILE_REAL_PERMUTE
#endif
#endif

// Public entry points. The internal mlk_kem_* names are function-like macros
// carrying the optional context parameter, and this test wants the same three
// calls the mlkem test makes anyway.
#include "mlkem_native.h"
}

#include <vx_spawn2.h>
#include <vx_intrinsics.h>
#include "common.h"

#if defined(PQC_KECCAK_SG25)
static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "SG25 requires a complete 32-lane ALU vector");
#endif

#if defined(PQC_PROFILE_ARITH) || defined(PQC_ARITH_COOP)
#include "mlk_arith_dispatch.h"
#endif

#if defined(PQC_NTT_COOP)
#include "mlkem_coop_ntt.h"
#include "mlk_coop_dispatch.h"
#if defined(PQC_CODEC_WARP)
#include "mlk_codec_w32.h"
#endif
#if defined(PQC_NOISE_WARP)
#include "mlk_noise_w32.h"
#endif
#if defined(PQC_LINEAR_WARP)
#include "mlk_linear_w32.h"
#endif
#if defined(PQC_ZEROIZE_WARP)
#include "mlk_zeroize_w32.h"
#endif

extern "C" __attribute__((noinline, used)) void mlk_profile_main(kernel_arg_t* arg) {
#else
__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
#endif
#if defined(PQC_KECCAK_SG25) && !defined(PQC_NTT_COOP)
  vx_tmc_one();
#endif
  if (threadIdx.x != 0) return;
  for (unsigned req = blockIdx.x; req < arg->requests; req += arg->workers) {

  auto s      = reinterpret_cast<uint8_t*>(arg->scratch_addr) + req * P_SCRATCH_LEN;
  auto counts = reinterpret_cast<uint32_t*>(arg->counts_addr) + req * MLK_PROF_COUNT;
  auto status = reinterpret_cast<int32_t*>(arg->status_addr) + req * 3;
  auto local_counts = mlk_prof_counts[vx_hart_id()];

  for (int i = 0; i < MLK_PROF_COUNT; ++i) {
    local_counts[i] = 0;
  }
  local_counts[MLK_PROF_ARM] = MLK_ARM_EXPECTED;

#if defined(PQC_PROFILE_ARITH)
  for (unsigned i = 0; i < 3; ++i) {
    mlk_arith_cycles[vx_warp_id()][i] = 0;
  }
#endif
#if defined(PQC_PROFILE_PHASES)
  uint64_t primitive_snapshots[3][3];
  for (unsigned i = 0; i < MLK_PHASE_COUNT; ++i) {
    mlk_phase_cycles[vx_warp_id()][i] = 0;
  }
#endif

#if defined(PQC_NTT_COOP)
  mlk_coop_args[vx_warp_id()].lanes = arg->ntt_lanes;
#endif
#if defined(PQC_ZEROIZE_WARP)
  mlk_zeroize_parallel[vx_warp_id()] = arg->workers == 1;
#endif

  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr) + req * P_CYCLE_COUNT;

  // Keep setup and result writes outside every resident request's interval.
  vx_barrier(1u << 8, arg->workers);

  // The arena is a bump allocator whose frees are no-ops, so it has to be reset
  // between operations or the three phases accumulate and the third runs out.
  uint64_t t0 = vx_rdcycle();
  mlk_arena_reset();
  status[0] = mlkem_keypair_derand(s + P_OFF_PK, s + P_OFF_SK, s + P_OFF_COINS_KP);
  uint64_t t1 = vx_rdcycle();
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_snapshot_primitives(primitive_snapshots[0]);
#endif
  mlk_arena_reset();
  status[1] = mlkem_enc_derand(s + P_OFF_CT, s + P_OFF_SS_ENC, s + P_OFF_PK,
                               s + P_OFF_COINS_ENC);
  uint64_t t2 = vx_rdcycle();
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_snapshot_primitives(primitive_snapshots[1]);
#endif
  mlk_arena_reset();
  status[2] = mlkem_dec(s + P_OFF_SS_DEC, s + P_OFF_CT, s + P_OFF_SK);
  uint64_t t3 = vx_rdcycle();
#if defined(PQC_PROFILE_PHASES)
  mlk_phase_snapshot_primitives(primitive_snapshots[2]);
#endif

  vx_barrier(1u << 8, arg->workers);
  cycles[P_CYCLE_KEYPAIR] = t1 - t0;
  cycles[P_CYCLE_ENCAPS] = t2 - t1;
  cycles[P_CYCLE_DECAPS] = t3 - t2;
  cycles[P_CYCLE_START] = t0;
  cycles[P_CYCLE_END] = t3;

#if defined(PQC_PROFILE_PHASES)
  for (unsigned i = 0; i < MLK_PHASE_COUNT; ++i) {
    cycles[P_CYCLE_PERMUTE + i] = mlk_phase_cycles[vx_warp_id()][i];
  }
  for (unsigned phase = 0; phase < 3; ++phase) {
    for (unsigned primitive = 0; primitive < 3; ++primitive) {
      const uint64_t previous = phase ? primitive_snapshots[phase - 1][primitive] : 0;
      cycles[P_CYCLE_KEYPAIR_PERMUTE + 3 * phase + primitive] =
          primitive_snapshots[phase][primitive] - previous;
    }
  }
#endif
#if defined(PQC_PROFILE_ARITH)
  for (unsigned i = 0; i < 3; ++i) {
    cycles[P_CYCLE_MULCACHE + i] = mlk_arith_cycles[vx_warp_id()][i];
  }
#endif

  for (int i = 0; i < MLK_PROF_COUNT; ++i) {
    counts[i] = local_counts[i];
  }
  }
}

#if defined(PQC_NTT_COOP)
// KMU initializes every launched lane before the KEM narrows to its leader.
__kernel __attribute__((naked)) void kernel_main(kernel_arg_t*) {
  asm volatile (
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "tail mlk_profile_main"
      :: "i"(RISCV_CUSTOM0));
}
#endif
