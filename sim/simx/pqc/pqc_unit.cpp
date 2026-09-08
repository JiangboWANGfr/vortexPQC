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

#include "pqc_unit.h"
#include "keccak_f1600.h"
#include "core.h"
#include "debug.h"
#include "mem/mem_block_pool.h"
#include <cstring>

using namespace vortex;

PqcUnit::PqcUnit(Core* core, SimChannel<LsuReq>& req_out, SimChannel<LsuRsp>& rsp_in)
  : core_(core)
  , req_out_(req_out)
  , rsp_in_(rsp_in)
{
  std::memset(state_, 0, sizeof(state_));
}

// Advance to the next active lane of the owning trace. Returns false when the
// trace has no lanes left, which is what finishes the instruction.
bool PqcUnit::next_lane() {
  auto& rs1_data = owner_->src_data[0];
  for (uint32_t t = (phase_ == Phase::IDLE ? 0 : lane_ + 1); t < VX_CFG_NUM_THREADS; ++t) {
    if (!owner_->tmask.test(t)) {
      continue;
    }
    lane_  = t;
    base_  = rs1_data[t].u;   // byte address of 25 uint64_t, the library's layout
    sent_  = 0;
    recvd_ = 0;
    phase_ = Phase::LOADING;
    return true;
  }
  return false;
}

// One beat: up to NUM_LSU_LANES 32-bit words, contiguous from base_ + 4*sent_.
void PqcUnit::issue_beat(bool write) {
  LsuReq req(VX_CFG_NUM_LSU_LANES);
  req.op = write ? MemOp::ST : MemOp::LD;
  // The beat's starting word index rides in the tag. LsuUnit stores it as the
  // entry's client_tag and restores it on the response, so a response can be
  // matched to its words without assuming responses come back in issue order or
  // in whole beats -- with a cache they do neither.
  req.tag  = sent_;
  req.cid  = owner_->cid;
  req.wid  = owner_->wid;
  req.uuid = owner_->uuid;
  uint32_t n = std::min<uint32_t>(VX_CFG_NUM_LSU_LANES, WORDS - sent_);
  for (uint32_t i = 0; i < n; ++i) {
    uint64_t addr = base_ + 4ull * (sent_ + i);
    req.mask.set(i);
    req.addrs.at(i) = addr;
    req.tids.at(i)  = lane_;
    if (write) {
      auto block = make_mem_block();
      std::memset(block->data(), 0, block->size());
      uint32_t off = addr & (VX_CFG_MEM_BLOCK_SIZE - 1);
      uint32_t w = state_[sent_ + i];
      for (uint32_t bo = 0; bo < 4; ++bo) {
        (*block)[off + bo] = uint8_t((w >> (8 * bo)) & 0xff);
      }
      req.data.at(i)   = block;
      req.byteen.at(i) = 0xfull << off;
    }
  }
  req_out_.send(req);
  sent_ += n;
}

// One-word load whose only purpose is to be ordered behind the stores.
void PqcUnit::issue_drain() {
  LsuReq req(VX_CFG_NUM_LSU_LANES);
  req.op   = MemOp::LD;
  req.tag  = WORDS;            // out of range: its data is discarded
  req.cid  = owner_->cid;
  req.wid  = owner_->wid;
  req.uuid = owner_->uuid;
  req.mask.set(0);
  req.addrs.at(0) = base_ + 4ull * (WORDS - 1);
  req.tids.at(0)  = lane_;
  req_out_.send(req);
}

void PqcUnit::step() {
  // Drain responses first so a load beat's data lands before the next is sent.
  while (!rsp_in_.empty()) {
    auto& rsp = rsp_in_.peek();
    const uint32_t word_base = (uint32_t)rsp.tag;
    if (word_base == WORDS) {
      // The drain load returned, so every store ahead of it is visible.
      // LANE_DONE and not IDLE: next_lane() keys its scan start on IDLE, so
      // reusing IDLE here would restart the scan at lane 0 and loop forever.
      phase_ = Phase::LANE_DONE;
      rsp_in_.pop();
      continue;
    }
    for (uint32_t i = 0; i < VX_CFG_NUM_LSU_LANES; ++i) {
      if (!rsp.mask.test(i)) {
        continue;
      }
      // Lane i of this beat carried word word_base + i, and the payload sits at
      // that word's own offset inside its cache line -- the same derivation
      // LsuUnit::process_response_step does from entry.lanes.at(lane).addr.
      const uint32_t widx = word_base + i;
      if (widx >= WORDS) {
        continue;
      }
      uint64_t addr = base_ + 4ull * widx;
      uint32_t off = addr & (VX_CFG_MEM_BLOCK_SIZE - 1);
      uint32_t w = 0;
      std::memcpy(&w, rsp.data.at(i)->data() + off, 4);
      state_[widx] = w;
      ++recvd_;
    }
    rsp_in_.pop();
  }

  if (owner_ == nullptr || req_out_.full()) {
    return;
  }

  if (phase_ == Phase::LOADING) {
    if (sent_ < WORDS) {
      issue_beat(false);
    } else if (recvd_ == WORDS) {
      // All 200 bytes in. Permute, then push them back out.
      uint64_t st[25];
      std::memcpy(st, state_, sizeof(st));
      pqc::keccak_f1600(st);
      std::memcpy(state_, st, sizeof(st));
      phase_ = Phase::PERMUTED;
      sent_  = 0;
    }
  } else if (phase_ == Phase::PERMUTED || phase_ == Phase::STORING) {
    phase_ = Phase::STORING;
    if (sent_ < WORDS) {
      issue_beat(true);
    } else {
      issue_drain();
      phase_ = Phase::DRAINING;
    }
  }
}

instr_trace_t* PqcUnit::process(instr_trace_t* trace) {
  if (owner_ == nullptr) {
    owner_ = trace;
    phase_ = Phase::IDLE;
    if (!next_lane()) {
      // No active lane: nothing to permute, retire immediately.
      owner_ = nullptr;
      return trace;
    }
    return nullptr;
  }
  if (owner_ != trace) {
    return nullptr;   // busy with another trace; retry
  }
  // step() flips DRAINING back to IDLE when the drain load returns.
  if (phase_ == Phase::LANE_DONE) {
    // This lane is done. Either move to the next one or finish the trace.
    if (next_lane()) {
      return nullptr;
    }
    owner_ = nullptr;
    phase_ = Phase::IDLE;
    return trace;
  }
  return nullptr;
}

uint32_t PqcUnit::latency(const instr_trace_t* trace) const {
  uint32_t jobs = 0;
  for (uint32_t t = 0; t < VX_CFG_NUM_THREADS; ++t) {
    if (trace->tmask.test(t)) {
      ++jobs;
    }
  }
  if (jobs == 0) {
    return 1;
  }
  const uint32_t engines = VX_CFG_PQC_NUM_ENGINES;
  const uint32_t passes = (jobs + engines - 1) / engines;
  return VX_CFG_PQC_KECCAK_LATENCY * passes;
}
