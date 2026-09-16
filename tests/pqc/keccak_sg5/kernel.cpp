// SG1 and SG5 Keccak-f1600, same source tree, same seeds, same permutation count.
//
// keccakf1600.c is included textually for the reason tests/pqc/mlkem_ntt_xn gives
// for poly.c: mlk_KeccakF_RoundConstants is a file-scope static (keccakf1600.c:193),
// and the cooperative arm must use the library's own constants rather than a copy
// that could drift from them. It also means the SG1 control is literally the
// function the real workload calls, not a re-implementation.

#include <vx_spawn2.h>
#include <vx_intrinsics.h>

extern "C" {
#include "src/common.h"
#include "src/fips202/keccakf1600.c"
}

#include "common.h"
#include "pqc_stack.h"
#if defined(PQC_KECCAK_PE)
#include <vx_pqc.h>
#endif

#include "permute.h"

// The chi->theta transpose is the one step SHFL provably cannot express
// (cooperative_ise_proposal.md S2.2): destination (lane d, slot s) needs
// (lane s, slot d), and slot d depends on the reading lane, which a SIMT
// register file cannot name. At plain ISA it is therefore a memory round trip.
// It cannot live on the stack -- vx_start.S:95 gives each hart its own slab, so
// a stack buffer would make each lane transpose a private copy and the arm would
// silently compute five independent wrong states while looking fast.
#define KS_MAX_WARPS  8
#define KS_MAX_GROUPS 6
static uint64_t ks_xpose[KS_MAX_WARPS][KS_MAX_GROUPS][KS_LANES_PER_STATE][5];

// Same seed function for both arms, so state g of SG5 is bit-identical to state g
// of SG1 and the host can check the two against each other as well as against the
// library.
static inline uint64_t ks_seed(unsigned state, unsigned word) {
  return 0x0123456789abcdefULL * (uint64_t)(state + 1) +
         0x9e3779b97f4a7c15ULL * (uint64_t)word;
}

__kernel void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto out    = reinterpret_cast<uint64_t*>(arg->states_addr);
  auto cycles = reinterpret_cast<uint64_t*>(arg->cycles_addr);

  const unsigned tid    = (unsigned)vx_thread_id();
  const unsigned W      = arg->lanes;
  const unsigned perms  = arg->perms;
  const unsigned wid    = (unsigned)vx_warp_id() & (KS_MAX_WARPS - 1);
  const int      cval   = (int)(W - 1);

  uintptr_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

#if defined(PQC_KECCAK_PE)
  if (arg->arm == KS_ARM_PE) {
    // One state per lane, permuted by the instruction. Same seeds and the same
    // host-side word-by-word check as the other two arms, so a wrong answer
    // names the word rather than just failing.
    uint64_t s[KS_WORDS];
    for (unsigned j = 0; j < KS_WORDS; ++j) s[j] = ks_seed(tid, j);
    // Read the state from the PROGRAM side between permutations. The loop
    // without this is PE -> PE -> PE, every access going through the PE's own
    // ordered client port, which never exercises the hazard that matters: the
    // instruction retires when its stores are ISSUED, not when they land, so a
    // program load right after it can race them. ML-KEM does exactly that on
    // every call and this loop did not.
    const uint64_t t0 = vx_rdcycle();
    for (unsigned p = 0; p < perms; ++p) {
      // -f 16: CPU stores into the state IMMEDIATELY before the instruction,
      // with nothing between them. wstall stops later instructions overtaking
      // the PE; it says nothing about whether an earlier store has landed by the
      // time the PE reads. This is the other half of the ordering boundary and
      // it needs its own test rather than an argument.
      if (arg->fences & 16u) {
        for (unsigned j = 0; j < KS_WORDS; ++j) s[j] ^= 0x9e3779b97f4a7c15ull;
      }
      vx_keccakf(s);
      // -f 8: a fence after the instruction. Purely diagnostic -- if this makes
      // the multi-lane read-back case pass, the defect is the instruction not
      // ordering its own writes against the caller, which is a semantics
      // question (keccak_ise_proposal.md S4) and not an AGU bug.
      if (arg->fences & 8u) vx_fence();
      if (arg->fences & 4u) {
        // Absorb: the program reads every word the PE just wrote and writes
        // some of them back, which is the shape mlk_keccakf1600_xor_bytes and
        // _extract_bytes give every sponge round. Not removable by the
        // compiler, and mirrored exactly in the host reference.
        uint64_t acc = 0;
        for (unsigned j = 0; j < KS_WORDS; ++j) acc ^= s[j];
        s[0] ^= acc;
      }
    }
    const uint64_t t1 = vx_rdcycle();
    for (unsigned j = 0; j < KS_WORDS; ++j) out[tid * KS_WORDS + j] = s[j];
    if (tid == 0) cycles[KS_CY_RUN] = t1 - t0;
  } else
#endif
  if (arg->arm == KS_ARM_SG1) {
    // One lane, one whole state. This is what schools A/B/C assume, and the
    // permutation is the library's own.
    uint64_t s[KS_WORDS];
    for (unsigned j = 0; j < KS_WORDS; ++j) s[j] = ks_seed(tid, j);

    const uint64_t t0 = vx_rdcycle();
    for (unsigned p = 0; p < perms; ++p) mlk_keccakf1600_permute(s);
    const uint64_t t1 = vx_rdcycle();

    for (unsigned j = 0; j < KS_WORDS; ++j) out[tid * KS_WORDS + j] = s[j];
    if (tid == 0) cycles[KS_CY_RUN] = t1 - t0;
  } else {
    const unsigned groups = W / KS_LANES_PER_STATE;
    if (tid >= groups * KS_LANES_PER_STATE) return;   // lane 15 at W = 16
    const unsigned g    = tid / KS_LANES_PER_STATE;
    const unsigned c    = tid % KS_LANES_PER_STATE;
    const unsigned base = g * KS_LANES_PER_STATE;

    // Lane c holds column A[c][0..4] = 5 words = 10 GPRs; five lanes hold the
    // whole 200-byte state with nothing in memory but the transpose buffer.
    uint64_t a[5];
    for (unsigned y = 0; y < 5; ++y) a[y] = ks_seed(g, c + 5 * y);

    const uint64_t t0 = vx_rdcycle();
    for (unsigned p = 0; p < perms; ++p)
      ks_sg5_permute(a, c, base, cval, ks_xpose[wid][g], arg->fences);
    const uint64_t t1 = vx_rdcycle();

    // The state is A[x][y] at index x + 5y, so lane c writes the five words it
    // owns: indices c, c+5, c+10, c+15, c+20.
    for (unsigned y = 0; y < 5; ++y) out[g * KS_WORDS + c + 5 * y] = a[y];
    if (tid == 0) cycles[KS_CY_RUN] = t1 - t0;
  }

  if (tid == 0) {
    cycles[KS_CY_SPAN]  = span;
    cycles[KS_CY_STACK] = pqc_stack_watermark(sp0);
  }
}
