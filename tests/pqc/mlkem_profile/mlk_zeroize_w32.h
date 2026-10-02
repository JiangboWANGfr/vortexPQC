#ifndef MLK_ZEROIZE_W32_H
#define MLK_ZEROIZE_W32_H

static_assert(VX_CFG_NUM_THREADS == 32 && VX_CFG_SIMD_WIDTH == 32 &&
              VX_CFG_NUM_ALU_LANES == 32,
              "ML-KEM zeroize requires a complete 32-lane ALU vector");

struct mlk_zeroize_args_t {
  uint8_t* output;
  size_t length;
};

static mlk_zeroize_args_t mlk_zeroize_args[VX_CFG_NUM_WARPS];
extern "C" uint32_t mlk_zeroize_parallel[VX_CFG_NUM_WARPS];
uint32_t mlk_zeroize_parallel[VX_CFG_NUM_WARPS];

extern "C" __attribute__((noinline, used)) void mlk_profile_zeroize_lanes() {
  const auto& args = mlk_zeroize_args[vx_warp_id()];
  const unsigned lane = vx_thread_id();
  size_t prefix = (4 - ((uintptr_t)args.output & 3)) & 3;
  if (prefix > args.length) {
    prefix = args.length;
  }
  if (lane == 0) {
    for (size_t i = 0; i < prefix; ++i) {
      args.output[i] = 0;
    }
  }
  uint32_t* words = reinterpret_cast<uint32_t*>(args.output + prefix);
  const size_t word_count = (args.length - prefix) / sizeof(uint32_t);
  for (size_t i = lane; i < word_count; i += 32) {
    words[i] = 0;
  }
  if (lane == 0) {
    for (size_t i = prefix + word_count * sizeof(uint32_t);
         i < args.length; ++i) {
      args.output[i] = 0;
    }
  }
  __syncthreads();
}

#if __riscv_xlen == 64
#define MLK_ZEROIZE_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLK_ZEROIZE_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLK_ZEROIZE_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLK_ZEROIZE_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mlk_profile_zeroize_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLK_ZEROIZE_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mlk_profile_zeroize_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLK_ZEROIZE_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

extern "C" void mlk_profile_zeroize_warp(void* ptr, size_t len) {
#if defined(PQC_PROFILE_PHASES) && !defined(PQC_PROFILE_PRIMITIVES)
  const mlk_phase_scope_t scope = mlk_phase_begin();
#endif
  auto& args = mlk_zeroize_args[vx_warp_id()];
  args.output = static_cast<uint8_t*>(ptr);
  args.length = len;
  vx_fence();
  __syncthreads();
  mlk_profile_zeroize_expand();
  asm volatile ("" : : "r"(ptr) : "memory");
#if defined(PQC_PROFILE_PHASES) && !defined(PQC_PROFILE_PRIMITIVES)
  mlk_phase_end(MLK_PHASE_ZEROIZE, scope);
#endif
}

#undef MLK_ZEROIZE_RESTORE_RA
#undef MLK_ZEROIZE_SAVE_RA

#endif
