#ifndef MLD_REJ_WARP_H
#define MLD_REJ_WARP_H

struct mld_rej_args_t {
  int32_t* output;
  const uint8_t* buffer;
  unsigned len;
  unsigned buflen;
  unsigned count;
};

static mld_rej_args_t mld_rej_args[MLD_PROF_SLOTS];
#if defined(PQC_REJ_ETA_WARP)
static mld_rej_args_t mld_rej_eta_args[MLD_PROF_SLOTS];
static unsigned mld_rej_eta_value[MLD_PROF_SLOTS];
#endif

extern "C" __attribute__((noinline, used)) void mld_profile_rej_uniform_lanes() {
  auto& args = mld_rej_args[mld_prof_slot()];
  const unsigned lane = vx_thread_id();
  const unsigned triplets = args.buflen / 3;
  const uint32_t earlier = lane ? (1u << lane) - 1u : 0u;
  unsigned count = 0;

  for (unsigned base = 0; base < triplets && count < args.len; base += 32) {
    const unsigned triplet = base + lane;
    const bool in_range = triplet < triplets;
    uint32_t value = 0;
    if (in_range) {
      const unsigned pos = 3 * triplet;
      value = (uint32_t)args.buffer[pos] |
              ((uint32_t)args.buffer[pos + 1] << 8) |
              ((uint32_t)args.buffer[pos + 2] << 16);
      value &= 0x7fffff;
    }
    const bool keep = in_range && value < 8380417u;
    const uint32_t mask = vx_vote_ballot(keep);
    const unsigned rank = count + __builtin_popcount(mask & earlier);
    if (keep && rank < args.len)
      args.output[rank] = (int32_t)value;
    const unsigned next = count + __builtin_popcount(mask);
    count = next < args.len ? next : args.len;
  }

  __syncthreads();
  if (lane == 0)
    args.count = count;
}

#if defined(PQC_REJ_ETA_WARP)
extern "C" __attribute__((noinline, used)) void mld_profile_rej_eta_lanes() {
  const unsigned slot = mld_prof_slot();
  auto& args = mld_rej_eta_args[slot];
  const unsigned lane = vx_thread_id();
  const unsigned eta = mld_rej_eta_value[slot];
  const unsigned candidates = 2 * args.buflen;
  const unsigned limit = eta == 2 ? 15 : 9;
  const uint32_t earlier = lane ? (1u << lane) - 1u : 0u;
  unsigned count = 0;

  for (unsigned base = 0; base < candidates && count < args.len; base += 32) {
    const unsigned candidate = base + lane;
    const bool in_range = candidate < candidates;
    uint32_t value = 0;
    if (in_range) {
      const uint8_t packed = args.buffer[candidate >> 1];
      value = (candidate & 1) ? packed >> 4 : packed & 0x0f;
    }
    const bool keep = in_range && value < limit;
    const uint32_t mask = vx_vote_ballot(keep);
    const unsigned rank = count + __builtin_popcount(mask & earlier);
    if (keep && rank < args.len) {
      if (eta == 2)
        value -= ((205 * value) >> 10) * 5;
      args.output[rank] = (int32_t)eta - (int32_t)value;
    }
    const unsigned next = count + __builtin_popcount(mask);
    count = next < args.len ? next : args.len;
  }

  __syncthreads();
  if (lane == 0)
    args.count = count;
}
#endif

#if __riscv_xlen == 64
#define MLD_REJ_SAVE_RA "sd ra, 8(sp)\n\t"
#define MLD_REJ_RESTORE_RA "ld ra, 8(sp)\n\t"
#else
#define MLD_REJ_SAVE_RA "sw ra, 12(sp)\n\t"
#define MLD_REJ_RESTORE_RA "lw ra, 12(sp)\n\t"
#endif

extern "C" __attribute__((naked, noinline)) void mld_profile_rej_uniform_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLD_REJ_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_rej_uniform_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLD_REJ_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}

#if defined(PQC_REJ_ETA_WARP)
extern "C" __attribute__((naked, noinline)) void mld_profile_rej_eta_expand() {
  asm volatile (
      "addi sp, sp, -16\n\t"
      MLD_REJ_SAVE_RA
      "li t0, -1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      "call mld_profile_rej_eta_lanes\n\t"
      ".insn r %0, 7, 0, x0, x0, x0\n\t"
      "li t0, 1\n\t"
      ".insn r %0, 0, 0, x0, t0, x0\n\t"
      MLD_REJ_RESTORE_RA
      "addi sp, sp, 16\n\t"
      "ret"
      :: "i"(RISCV_CUSTOM0));
}
#endif

extern "C" int mld_profile_rej_uniform(int32_t* r, unsigned len,
                                        const uint8_t* buf, unsigned buflen) {
  auto& args = mld_rej_args[mld_prof_slot()];
  args.output = r;
  args.buffer = buf;
  args.len = len;
  args.buflen = buflen;
  vx_fence();
  __syncthreads();
  mld_profile_rej_uniform_expand();
  return (int)args.count;
}

#if defined(PQC_REJ_ETA_WARP)
extern "C" int mld_profile_rej_eta(int32_t* r, unsigned len,
                                    const uint8_t* buf, unsigned buflen,
                                    unsigned eta) {
  const unsigned slot = mld_prof_slot();
  auto& args = mld_rej_eta_args[slot];
  args.output = r;
  args.buffer = buf;
  args.len = len;
  args.buflen = buflen;
  mld_rej_eta_value[slot] = eta;
  vx_fence();
  __syncthreads();
  mld_profile_rej_eta_expand();
  return (int)args.count;
}
#endif

#undef MLD_REJ_SAVE_RA
#undef MLD_REJ_RESTORE_RA

#endif
