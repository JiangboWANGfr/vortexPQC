// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "types.h"
#include "instr_trace.h"
#include <cstdint>

namespace vortex {

class Core;

// The Keccak-f1600 PE behind PqcType::KECCAKF. A plain helper owned by SfuUnit,
// the shape DxaUnit uses; SfuUnit is the SimObject.
//
// MEMORY GOES THROUGH THE LSU, and that is not a style choice. Reading and
// writing ProcessorImpl::ram() directly was tried and does not work, in both
// directions: against the default write-back D-cache the PE reads
// sim/common/mem.cpp:434's 0xbaadf00d unallocated-page sentinel because the
// kernel's stack writes are still dirty in the cache, and rebuilt write-through
// the reads come right while the writes go stale, the PE writing ram() behind
// the cache and the program reading back its own clean line. ram() is right for
// what sim/simx/raster/raster_core.cpp:929 uses it for -- peeking at a depth
// buffer the OM path has committed -- and wrong for hot data the kernel owns.
//
// So the PE gets an LsuReq/LsuRsp client port pair on LsuUnit, bound in
// core.cpp beside the TCU's, and the lmem_switch does the local-versus-global
// decode so the PE never has to know where a state lives.
//
// ORDERING AGAINST THE CALLER, and this is not a modelling detail.
// A store gets no response in this model -- LsuUnit allocates a pending entry
// only for loads and AMOs (lsu_unit.cpp, `if (!is_write || is_amo)`), because an
// entry for a store would never be released and fence release is gated on
// pending_reqs.empty(). So the PE cannot see its own writes land. Retiring the
// instruction when the last store is merely ISSUED loses the race against the
// program's very next load, which is exactly what the library does on every
// sponge round: xor_bytes writes the state, the PE permutes it, extract_bytes
// reads it straight back.
//
// A test caught this only once it was written to read the state back between
// permutations (tests/pqc/keccak_sg5 -f 4); a PE-to-PE loop passes because those
// accesses share the PE's own ordered client port.
//
// The fix here is a DRAIN: after the last store beat, read all 50 words back
// and wait for the responses. A one-word drain was tried first and left twelve
// of the thirteen lines unordered; a fence after the instruction only halved
// the damage, because FenceController waits on pending_reqs.empty() and stores
// allocate no entry there. Reading every word closes both holes -- the loads do
// allocate entries, so the PE's traffic becomes visible to fences, and their
// responses are the completion signal the store path cannot give. It costs a
// second round trip, which is honest -- real hardware needs an
// acknowledgement too, and
// keccak_ise_proposal.md S4 is titled "the one genuinely open question: ordering
// against the caller's stores". This is that question, and the answer the model
// picks is the conservative one.
//
// The other half of that is an IMPLICIT ACQUIRE, and it took a second bug to
// find. A store commits as soon as the LSU dispatches it -- it allocates no
// pending entry and leaves pending_instrs_ right away -- while its data is
// still travelling to the cache. So the PE, which injects at lmem_switch, can
// read a word the caller has already "written". With one warp nothing else is
// competing for the dispatch slot and it never showed; from two warps up it
// did, and mlkem_width -b 4 -t 1 had warp 0 correct and warps 1..3 wrong in
// 70, 64 and 63 of their 156 permutations, always in the last beat:
//     w0  in[24] = 0000000000000000      <- correct
//     w1  in[24] = ea29fda51a713d91      <- a previous state, still there
//     w2  in[24] = ea29fda500000000      <- one 32-bit word stale, one fresh
// A watch on that address read the inversion straight off:
//     cy=181946 pqc  LD addr=fffe7078
//     cy=181954 core ST addr=fffe7078    <- the caller's store, 8 cycles late
// The guard is has_pending_instrs(wid) before the first load. The warp is
// wstall'd on this instruction, so any other trace of it still in flight is
// necessarily older, and holding for them is nearly free: 161 cycles on the
// 11.71 M-cycle ML-KEM profile run (0.0014%), and exactly zero on the plain
// single-warp build, where nothing else is ever in flight to wait for.
//
// TWO TRAPS WORTH LEAVING WRITTEN DOWN.
// The first is that instr_trace_t::uuid is generated under #ifndef NDEBUG in
// scheduler.cpp, so in a release build every uuid is 0. Two earlier versions of
// this guard ordered on uuid, compiled clean, and were exactly as vacuous as
// `0 < 0` -- both runs reproduced the failing cycle count to the bit, which is
// the tell: a guard that changes nothing is not a guard that was not needed.
// The second is that this client's forward block in LsuUnit::dispatch runs
// ahead of the block's own req_queue and returns on success, so the PE has
// priority over the core -- a risk recorded when this client was written, and
// this is it arriving. How much of the eight-cycle window it opened is untested:
// the vacuous uuid gate proved nothing about what was in req_queue, and the
// working guard makes the question moot by holding the PE instead.
//
// ONE LANE AT A TIME. The unit serves the active lanes of a trace serially:
// simpler, and the timing model already serialises on VX_CFG_PQC_NUM_ENGINES,
// so a parallel AGU would buy fidelity the latency model then throws away.
class PqcUnit {
public:
  PqcUnit(Core* core, SimChannel<LsuReq>& req_out, SimChannel<LsuRsp>& rsp_in);

  // Returns nullptr while the permutation is still in flight -- SfuUnit leaves
  // the trace in its input queue and retries next cycle, the way DxaUnit
  // back-pressures. Returns the trace once every active lane's state has been
  // read, permuted and written back.
  instr_trace_t* process(instr_trace_t* trace);

  // Drives the AGU: one request per cycle out, responses drained in.
  void step();

  // The SFU's output delay, and it is deliberately minimal: the engine's own
  // time is now spent INSIDE the unit, holding owner_, because that is what the
  // hardware does (VX_pqc_agu sits in PQC_PERM while VX_pqc_keccak_f1600 runs).
  //
  // An earlier version charged KECCAK_LATENCY * ceil(jobs/ENGINES) here, at the
  // SFU output, AFTER releasing owner_. For one warp that is the same total, but
  // it lets the next warp start its own transfers during a window in which the
  // hardware's engine is busy, so the model overstated multi-warp throughput --
  // measured as a 4.6% span divergence against rtlsim at eight warps, converging
  // to 1.9% at four. Holding the unit instead is both more faithful and what
  // makes a multi-warp cycle comparison mean anything.
  //
  // ceil(jobs/ENGINES) survives the move: the hold is charged once per group of
  // ENGINES lanes, so the per-core versus per-lane A/B is the same sweep it was
  // (keccak_ise_proposal.md S5), and at ENGINES=1 it matches the RTL exactly.
  uint32_t latency(const instr_trace_t* trace) const;

private:
  static const uint32_t WORDS = 50;   // 25 x uint64_t as 32-bit LSU words

  enum class Phase { IDLE, LOADING, PERMUTED, STORING, DRAINING, LANE_DONE };
  // DRAINING re-reads all 50 words; see the note above on why one word is not enough.

  bool next_lane();
  void issue_beat(bool write);
  // no separate drain: the read-back IS the drain

  Core* core_;
  SimChannel<LsuReq>& req_out_;
  SimChannel<LsuRsp>& rsp_in_;

  Phase          phase_ = Phase::IDLE;
  instr_trace_t* owner_ = nullptr;
  uint32_t       lane_  = 0;          // active lane being served
  uint64_t       base_  = 0;          // that lane's rs1
  uint32_t       perm_wait_ = 0;      // cycles left in PERMUTED; see the note
  uint32_t       lane_seq_  = 0;      // lanes served so far in this trace
  uint32_t       sent_  = 0;          // words requested
  uint32_t       recvd_ = 0;          // words returned
  uint32_t       state_[WORDS];       // the 200 bytes, as 32-bit words
};

} // namespace vortex
