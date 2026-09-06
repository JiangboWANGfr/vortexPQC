// FIPS-202 backend: PQRV's hand-written RV32 Keccak-f1600 assembly.
//
// This is the "optimized software" column of the Keccak baseline, selected with
// KECCAK=asm. The default build stays on mlkem-native's reference C, and the
// two are reported side by side -- the point is not to have the fastest
// baseline but to know how much of the gap to one is software and how much is
// the machine.
//
// The permutation is pqc/third_party/pqrv/fips202_rv32im.S, vendored verbatim
// (MIT, Jipeng Zhang; see that directory's README for the sha256 and commit).
// It is plain RV32IM with no B-extension instructions, and lane-complementing
// rather than bit-interleaved, so the state layout is the one mlkem-native
// already uses -- a genuine drop-in through the library's own hook, with the
// submodule untouched.
#ifndef VORTEX_KECCAK_PQRV_H
#define VORTEX_KECCAK_PQRV_H

#if !defined(__ASSEMBLER__)

#include "src/fips202/native/api.h"

// The library is compiled inside an extern "C" block by the PQC kernels, but
// not every consumer does that, and a C++ mangled reference to an assembly
// symbol fails at link time with a message that does not name the cause.
#if defined(__cplusplus)
extern "C" {
#endif
void KeccakF1600_StatePermute_RV32ASM(uint64_t *state);
#if defined(__cplusplus)
}
#endif

#define MLK_USE_NATIVE_FIPS202_X1
static MLK_INLINE int mlk_keccak_f1600_x1_native(uint64_t *state)
{
  KeccakF1600_StatePermute_RV32ASM(state);
  return MLK_NATIVE_FUNC_SUCCESS;
}

// x4 is deliberately not hooked: mlk_keccakf1600x4_permute falls back to four
// x1 calls, so the batched path gets the assembly too, and leaving the x4 hook
// alone keeps the permutation count identical to the C column. Comparing two
// baselines that ran different numbers of permutations would not be comparing
// baselines.

#endif /* !__ASSEMBLER__ */
#endif /* VORTEX_KECCAK_PQRV_H */
