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

// rho offsets, indexed [x][y] -- the transpose of how FIPS 202 Table 2 prints
// them, because SG5's lane index is x.
static const unsigned char KS_RHO[5][5] = {
    /* x=0 */ {0, 36, 3, 41, 18},
    /* x=1 */ {1, 44, 10, 45, 2},
    /* x=2 */ {62, 6, 43, 15, 61},
    /* x=3 */ {28, 55, 25, 21, 56},
    /* x=4 */ {27, 20, 39, 8, 14},
};

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

static inline uint64_t ks_rol64(uint64_t v, unsigned n) {
  return n ? ((v << n) | (v >> (64 - n))) : v;
}

// mask = 0 makes minLane 0 and lane_idx = bval, so this is an arbitrary
// full-warp gather; cval bounds the index (VX_alu_int.sv:184-217). bval is a
// per-lane register value, which is what lets each lane name a different source.
static inline uint64_t ks_shfl64(uint64_t v, int src, int cval) {
  uint32_t lo = (uint32_t)vx_shfl_idx((size_t)(uint32_t)v, src, cval, 0);
  uint32_t hi = (uint32_t)vx_shfl_idx((size_t)(uint32_t)(v >> 32), src, cval, 0);
  return ((uint64_t)hi << 32) | lo;
}

// One permutation, five lanes. `a[y]` holds A[c][y] on entry and on exit.
static void ks_sg5_permute(uint64_t a[5], unsigned c, unsigned base, int cval,
                           uint64_t (*xp)[5], unsigned fences) {
  const int lane_m = (int)(base + (c + 4) % 5);
  const int lane_p = (int)(base + (c + 1) % 5);
  int pi_src[5];
  for (unsigned y = 0; y < 5; ++y) pi_src[y] = (int)(base + (3 * c + y) % 5);

  for (unsigned round = 0; round < 24; ++round) {
    // theta, part 1: C[x] = XOR_y A[x][y]. Lane-internal -- this is what the
    // column layout buys, and it is the whole reason SG5 beats SG5r.
    const uint64_t C = a[0] ^ a[1] ^ a[2] ^ a[3] ^ a[4];

    // theta, part 2: D[x] = C[x-1] ^ ROL64(C[x+1], 1). Two neighbours.
    const uint64_t D = ks_shfl64(C, lane_m, cval) ^
                       ks_rol64(ks_shfl64(C, lane_p, cval), 1);

    // theta part 3 fused with rho. The rotate amount is per-lane, which is what
    // costs SG5 a branch-free 64-bit rotate per word instead of an immediate one.
    uint64_t t[5];
    for (unsigned y = 0; y < 5; ++y) t[y] = ks_rol64(a[y] ^ D, KS_RHO[c][y]);

    // pi. B[y][(2x+3y) mod 5] = A[x][y], stored with lane = B's SECOND index, so
    // lane c slot y receives B[y][c] from lane (3c+y) mod 5, slot y. Slot is
    // preserved, so this is five lane bijections and not a scatter.
    uint64_t b[5];
    for (unsigned y = 0; y < 5; ++y) b[y] = ks_shfl64(t[y], pi_src[y], cval);

    // chi. Lane c now holds B[s][c] at slot s for every s, and chi combines
    // three consecutive first indices at one second index -- lane-internal.
    uint64_t n[5];
    for (unsigned s = 0; s < 5; ++s)
      n[s] = b[s] ^ ((~b[(s + 1) % 5]) & b[(s + 2) % 5]);

    // iota. A'[0][0] sits at lane c == 0, slot 0.
    if (c == 0) n[0] ^= mlk_KeccakF_RoundConstants[round];

    // transpose. Lane c slot s holds A'[s][c]; the next theta wants lane x slot y
    // to hold A'[x][y], i.e. what is at lane y slot x.
    for (unsigned s = 0; s < 5; ++s) xp[c][s] = n[s];
    if (fences >= 1) vx_fence();
    for (unsigned y = 0; y < 5; ++y) a[y] = xp[y][c];
    if (fences >= 2) vx_fence();
  }
}

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

  uint32_t sp0;
  const uint32_t span = pqc_stack_paint(&sp0);

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
