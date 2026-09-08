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
// The fix here is a DRAIN: after the last store beat, issue a single one-word
// load and wait for its response. The switch orders that load behind the stores
// ahead of it, so its return means they are visible. It costs one round trip,
// which is honest -- real hardware needs an acknowledgement too, and
// keccak_ise_proposal.md S4 is titled "the one genuinely open question: ordering
// against the caller's stores". This is that question, and the answer the model
// picks is the conservative one.
//
// !!! KNOWN FAILURE: THREE OR MORE LANES WITH A PROGRAM READ-BACK. !!!
// tests/pqc/keccak_sg5 -a pe, p=4, with the all-words drain in place:
//     t=1 f=4   bad=0        t=1 f=0   bad=0
//     t=2 f=4   bad=0        t=4 f=0   bad=0
//     t=3 f=4   FAIL
//     t=4 f=4   FAIL (two whole states of four)
// Neither condition alone breaks it: one and two lanes are fine with the
// read-back, and four lanes are fine without it. The threshold is at three
// lanes, which is a number worth explaining -- VX_CFG_NUM_LSU_LANES is 4 and
// VX_CFG_LSU_PENDING_SIZE is 8, and 50 words is 13 beats per phase per lane.
//
// STOP GUESSING AT THIS POINT. Four hypotheses have been tried and each shifted
// the symptom without predicting it. The next step is not another guess: log
// every PQC request and response with its cycle, phase, lane and tag, then diff
// a passing t=2 run against a failing t=3 one. The first divergence names the
// mechanism.
//
// The
// per-lane drain therefore orders THAT lane's stores but something about
// serving lanes one after another leaves an earlier lane's writes behind. The
// The diagnostic ran. It is the LAST two lanes -- states 2 and 3 of 4 -- and
// each is wrong in all 25 words rather than a few.
//
// AND THE AGU IS NOT THE DEFECT. Tracing every lane's completion shows all four
// finishing cleanly on every call, recvd=50 and sent=50, at four distinct
// 8 KB-spaced per-hart stack addresses:
//     lane=0 base=0xfffeff30 DONE recvd=50 sent=50
//     lane=1 base=0xfffedf30 DONE recvd=50 sent=50
//     lane=2 base=0xfffebf30 DONE recvd=50 sent=50
//     lane=3 base=0xfffe9f30 DONE recvd=50 sent=50
// The PE reads, permutes and writes back the right 200 bytes for every lane. So
// the remaining suspect is visibility rather than work: the drain load for a
// lane may be satisfied by the cache line its own last store just filled, which
// returns immediately and orders nothing behind it, leaving the other 49 words
// in flight when the instruction retires. That would explain why one lane is
// fine (its stores have many cycles of the following lanes to drain) and the
// last lanes are not.
//
// THE DRAIN NOW RE-READS ALL 50 WORDS, and the one-word version is why.
// A fence after the instruction halved the damage (bad=50 -> bad=25) rather than
// removing it, which says the fence does not cover this client: FenceController
// waits on pending_reqs.empty(), stores allocate no entry there, so a fence can
// retire while the PE's writes are still in flight. A one-word drain had the
// same hole -- it orders one line and leaves twelve.
// Reading every word back closes both. The loads allocate pending entries, so
// the PE's traffic is now visible to fences and to drained(), and their
// responses are the completion signal the store path cannot give. It costs a
// second round trip per permutation, which is real and shows up in the cycle
// count -- and it is what hardware would pay too, in a write-acknowledge or a
// fence, for an instruction that must not retire before its writes are seen.
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

  // Timing on top of the modelled traffic. jobs = active lanes, each a whole
  // permutation, so with fewer engines than jobs they serialise:
  //
  //   latency = KECCAK_LATENCY * ceil(jobs / ENGINES)
  //
  // THAT ceil() IS THE ENTIRE PER-CORE VERSUS PER-LANE A/B. ENGINES=1 is one
  // engine shared by the core, ENGINES=SIMD_WIDTH is one per lane, and the
  // encoding, semantics and compiled binary are identical either way -- a
  // parameter sweep over one source tree rather than two designs wearing one
  // name (keccak_ise_proposal.md S5).
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
  uint32_t       sent_  = 0;          // words requested
  uint32_t       recvd_ = 0;          // words returned
  uint32_t       state_[WORDS];       // the 200 bytes, as 32-bit words
};

} // namespace vortex
