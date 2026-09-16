#ifndef KECCAK_SG5_PERMUTE_H
#define KECCAK_SG5_PERMUTE_H

#include <stdint.h>
#include <vx_intrinsics.h>

// rho offsets, indexed [x][y] -- the transpose of how FIPS 202 Table 2 prints
// them, because SG5's lane index is x.
static const unsigned char KS_RHO[5][5] = {
    /* x=0 */ {0, 36, 3, 41, 18},
    /* x=1 */ {1, 44, 10, 45, 2},
    /* x=2 */ {62, 6, 43, 15, 61},
    /* x=3 */ {28, 55, 25, 21, 56},
    /* x=4 */ {27, 20, 39, 8, 14},
};

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

#endif
