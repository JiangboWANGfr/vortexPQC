#include <vx_spawn2.h>
#include <vx_pqc.h>

#include "common.h"

#define NTTMUL_STRINGIFY_(value) #value
#define NTTMUL_STRINGIFY(value) NTTMUL_STRINGIFY_(value)

// The int16_t intrinsic inserts narrowing ops, so keep this latency chain at XLEN.
static inline __attribute__((always_inline)) uint64_t nttmul_raw64_chain(
    intptr_t& value, intptr_t factor) {
#if __riscv_xlen == 32
  __rdcycle_time start;
  __rdcycle_time end;
  asm volatile (
      ".insn r %[op], 7, 0, x0, x0, x0\n\t"
      "csrr %[start_hi], %[cycle_hi]\n\t"
      "csrr %[start_lo], %[cycle_lo]\n\t"
      ".rept " NTTMUL_STRINGIFY(NTTMUL_RAW_ROUNDS) "\n\t"
      ".insn r %[op], %[f3], %[f7], %[value], %[value], %[factor]\n\t"
      ".endr\n\t"
      ".insn r %[op], 7, 0, x0, x0, x0\n\t"
      "csrr %[end_lo], %[cycle_lo]\n\t"
      "csrr %[end_hi], %[cycle_hi]"
      : [value] "+&r" (value),
        [start_hi] "=&r" (start.hi), [start_lo] "=&r" (start.lo),
        [end_lo] "=&r" (end.lo), [end_hi] "=&r" (end.hi)
      : [op] "i" (VX_PQC_EXT_OPCODE),
        [f3] "i" (VX_PQC_F3_NTTMUL_K), [f7] "i" (VX_PQC_FUNCT7),
        [factor] "r" (factor),
        [cycle_hi] "i" (VX_CSR_MCYCLE_H), [cycle_lo] "i" (VX_CSR_MCYCLE)
      : "memory");
  return vx_rdcycle_sync_diff(start, end);
#elif __riscv_xlen == 64
  uint64_t start;
  uint64_t end;
  asm volatile (
      ".insn r %[op], 7, 0, x0, x0, x0\n\t"
      "csrr %[start], %[cycle]\n\t"
      ".rept " NTTMUL_STRINGIFY(NTTMUL_RAW_ROUNDS) "\n\t"
      ".insn r %[op], %[f3], %[f7], %[value], %[value], %[factor]\n\t"
      ".endr\n\t"
      ".insn r %[op], 7, 0, x0, x0, x0\n\t"
      "csrr %[end], %[cycle]"
      : [value] "+&r" (value), [start] "=&r" (start), [end] "=&r" (end)
      : [op] "i" (VX_PQC_EXT_OPCODE),
        [f3] "i" (VX_PQC_F3_NTTMUL_K), [f7] "i" (VX_PQC_FUNCT7),
        [factor] "r" (factor), [cycle] "i" (VX_CSR_MCYCLE)
      : "memory");
  return end - start;
#else
#error "Unsupported RISC-V XLEN"
#endif
}

static __attribute__((noinline)) void nttmul_raw64(
    const int16_t* input_a, const int16_t* input_b,
    int32_t* output, uint64_t* cycles, uint32_t count, uint32_t stride,
    uint32_t first) {
  for (uint32_t i = first; i < count; i += stride) {
    intptr_t value = input_a[i];
    const intptr_t factor = input_b[i];
    const uint64_t elapsed = nttmul_raw64_chain(value, factor);
    output[i] = (int16_t)value;
    cycles[i] = elapsed;
  }
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  const auto input_a = reinterpret_cast<const int16_t*>(arg->input_a_addr);
  const auto input_b = reinterpret_cast<const int16_t*>(arg->input_b_addr);
  auto output = reinterpret_cast<int32_t*>(arg->output_addr);
  const uint32_t stride = blockDim.x * gridDim.x;
  const uint32_t first = blockIdx.x * blockDim.x + threadIdx.x;

  if (arg->mode == NTTMUL_MODE_RAW64) {
    auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);
    nttmul_raw64(input_a, input_b, output, cycles, arg->count, stride, first);
    return;
  }

  for (uint32_t i = first; i < arg->count; i += stride) {
    int16_t value = input_a[i];
    for (uint32_t round = 0; round < arg->rounds; ++round) {
      value = vx_nttmul_k(value, input_b[i]);
    }
    output[i] = value;
  }
}

#undef NTTMUL_STRINGIFY
#undef NTTMUL_STRINGIFY_
