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

namespace vortex {

class Core;

// The Keccak-f1600 PE behind PqcType::KECCAKF. A plain helper owned by SfuUnit,
// the shape DxaUnit uses; SfuUnit is the SimObject.
//
// !!! THIS UNIT IS NOT YET COHERENT AND ITS RESULTS ARE WRONG. !!!
//
// Reading and writing the functional store directly does not work, and the two
// experiments that establish that are worth recording so nobody spends the
// afternoon on it again:
//
//   1. Against the default write-back D-cache the unit reads 0xbaadf00d --
//      sim/common/mem.cpp:434's unallocated-page sentinel. The program's stack
//      writes are sitting dirty in the D-cache and have never reached ram().
//   2. Rebuilt with -DVX_CFG_DCACHE_WRITEBACK=0 the READ direction comes right --
//      real state arrives and each call permutes a different one -- and the
//      answer is still wrong, because the WRITE direction is now the stale one:
//      the PE writes ram() behind the cache's back and the program then reads
//      its own clean, stale line.
//
// Both directions need coherence, so the PE has to reach memory the way the TCU
// AGU does: an LsuReq/LsuRsp client port pair on LsuUnit, bound in core.cpp
// beside TcuReqIn/TcuRspOut. That is what next_steps_recipes.md calls the
// Cardinal Rule, and it is not avoidable. ProcessorImpl::ram() is fine for what
// sim/simx/raster/raster_core.cpp:929 uses it for -- peeking at a depth buffer
// the OM path has already committed -- and not for hot data the kernel owns.
//
// TRANSACTION-LEVEL MEMORY, AND THIS IS THE MODEL'S ONE REAL ASSUMPTION.
// The instruction reads 200 bytes, permutes them and writes them back. This
// model does the read and write against the functional store -- ProcessorImpl::
// ram(), the same peek sim/simx/raster/raster_core.cpp:929 uses for early-Z --
// and charges a flat VX_CFG_PQC_MEM_LATENCY for the traffic, rather than issuing
// beats into the cache hierarchy through an LSU client port the way the TCU's
// AGU does.
//
// What that buys: a measured end-to-end speedup today, with no changes to
// LsuUnit. What it costs: the PE's memory traffic is invisible to the caches, so
// it neither hits nor pollutes them, and the flat charge is a guess. That is why
// VX_CFG_PQC_MEM_LATENCY is a config knob and why the paper reports a
// sensitivity curve over it instead of a single number -- the datapath latency
// comes from synthesis and is solid, this one does not and is not. An LSU client
// port is the Stage 2 refinement if the curve says the answer depends on it.
class PqcUnit {
public:
  PqcUnit(Core* core) : core_(core) {}

  // Functional: permute the state each active lane points at, in place.
  void execute(instr_trace_t* trace);

  // Timing. jobs = active lanes, each one a whole permutation, so with fewer
  // engines than jobs the engine is reused and the permutations serialise.
  //
  //   latency = MEM + KECCAK * ceil(jobs / ENGINES)
  //
  // THAT ceil() IS THE ENTIRE PER-CORE VERSUS PER-LANE A/B. ENGINES=1 is one
  // engine shared by the core; ENGINES=SIMD_WIDTH is one per lane. The encoding,
  // the semantics and the compiled binary are identical either way, which is
  // what makes it a parameter sweep over one source tree rather than two designs
  // wearing one name (keccak_ise_proposal.md S5).
  uint32_t latency(const instr_trace_t* trace) const;

private:
  Core* core_;
};

} // namespace vortex
