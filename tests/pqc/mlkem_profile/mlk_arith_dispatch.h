#ifndef MLK_ARITH_DISPATCH_H
#define MLK_ARITH_DISPATCH_H

#if defined(PQC_PROFILE_ARITH)
static uint64_t mlk_arith_cycles[VX_CFG_NUM_WARPS][3];

static inline uint64_t mlk_arith_timestamp() {
  asm volatile ("" ::: "memory");
  const uint64_t value = vx_rdcycle();
  asm volatile ("" ::: "memory");
  return value;
}
#endif

#if defined(PQC_ARITH_COOP)
#include "mlkem_coop_arith.h"

struct mlk_arith_args_t {
  int16_t* output;
  const int16_t* a;
  const int16_t* b;
  const int16_t* cache;
  unsigned operation;
};

static mlk_arith_args_t mlk_arith_args[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_arith_lanes() {
  const auto& args = mlk_arith_args[vx_warp_id()];
#if defined(PQC_ARITH_NTTMUL)
  constexpr bool use_nttmul = true;
#else
  constexpr bool use_nttmul = false;
#endif
  if (args.operation == 0) {
    mlk_mulcache_w32<use_nttmul>(args.output, args.a, vx_thread_id());
  } else if (args.operation == 1) {
    mlk_basemul_w32<use_nttmul>(args.output, args.a, args.b, args.cache,
                               vx_thread_id());
  } else {
    mlk_reduce_w32(args.output, vx_thread_id());
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLK_ARITH_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_ARITH_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_ARITH_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_ARITH_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

// Workers return to the leader-only continuation with their KMU stacks intact.
extern "C" __attribute__((naked, noinline)) void mlk_profile_arith_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_ARITH_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mlk_profile_arith_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_ARITH_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#undef MLK_ARITH_RESTORE_RA
#undef MLK_ARITH_SAVE_RA

static inline void mlk_arith_dispatch(unsigned operation, int16_t* output,
                                      const int16_t* a, const int16_t* b,
                                      const int16_t* cache) {
  auto& args = mlk_arith_args[vx_warp_id()];
  args.output = output;
  args.a = a;
  args.b = b;
  args.cache = cache;
  args.operation = operation;
  __syncthreads();
  mlk_profile_arith_expand();
}
#endif

extern "C" __attribute__((noinline)) void mlk_profile_reduce(int16_t* p) {
#if defined(PQC_PROFILE_ARITH)
  const uint64_t start = mlk_arith_timestamp();
#endif
#if defined(PQC_ARITH_REDUCE)
  mlk_arith_dispatch(2, p, nullptr, nullptr, nullptr);
#else
  mlk_poly_reduce_c(reinterpret_cast<mlk_poly*>(p));
#endif
#if defined(PQC_PROFILE_ARITH)
  mlk_arith_cycles[vx_warp_id()][2] += mlk_arith_timestamp() - start;
#endif
}

extern "C" __attribute__((noinline)) void mlk_profile_mulcache(
    int16_t* x, const int16_t* a) {
#if defined(PQC_PROFILE_ARITH)
  const uint64_t start = mlk_arith_timestamp();
#endif
#if defined(PQC_ARITH_MULCACHE)
  mlk_arith_dispatch(0, x, a, nullptr, nullptr);
#else
  mlk_poly_mulcache_compute_c(reinterpret_cast<mlk_poly_mulcache*>(x),
                            reinterpret_cast<const mlk_poly*>(a));
#endif
#if defined(PQC_PROFILE_ARITH)
  mlk_arith_cycles[vx_warp_id()][0] += mlk_arith_timestamp() - start;
#endif
}

extern "C" __attribute__((noinline)) void mlk_profile_basemul(
    int16_t* r, const int16_t* a, const int16_t* b, const int16_t* cache) {
#if defined(PQC_PROFILE_ARITH)
  const uint64_t start = mlk_arith_timestamp();
#endif
#if defined(PQC_ARITH_BASEMUL)
  mlk_arith_dispatch(1, r, a, b, cache);
#else
  mlk_polyvec_basemul_acc_montgomery_cached_c(
      reinterpret_cast<mlk_poly*>(r), reinterpret_cast<const mlk_polyvec*>(a),
      reinterpret_cast<const mlk_polyvec*>(b),
      reinterpret_cast<const mlk_polyvec_mulcache*>(cache));
#endif
#if defined(PQC_PROFILE_ARITH)
  mlk_arith_cycles[vx_warp_id()][1] += mlk_arith_timestamp() - start;
#endif
}

#endif
