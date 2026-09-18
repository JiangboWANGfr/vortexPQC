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

#include "alu_unit.h"
#include <iostream>
#include <iomanip>
#include <string.h>
#include <assert.h>
#include <util.h>
#include "debug.h"
#include "core.h"
#include "scheduler.h"
#include "constants.h"

using namespace vortex;

#ifdef VX_CFG_EXT_KSG25_ENABLE
static_assert(VX_CFG_NUM_THREADS == 32
           && VX_CFG_SIMD_WIDTH == 32 && VX_CFG_NUM_ALU_LANES == 32,
              "KSG25 requires 32 threads, SIMD lanes, and ALU lanes");
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
static_assert(VX_CFG_NUM_THREADS == 32
           && VX_CFG_SIMD_WIDTH == 32 && VX_CFG_NUM_ALU_LANES == 32,
              "KROUND25 requires 32 threads, SIMD lanes, and ALU lanes");
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
namespace {

int32_t signed16(uint32_t value) {
	int32_t result = static_cast<int32_t>(value & 0xffffu);
	return result < 0x8000 ? result : result - 0x10000;
}

int64_t signed32(uint32_t value) {
  return value < 0x80000000u ? value : int64_t(value) - (int64_t(1) << 32);
}

int32_t montgomery_reduce_d(int32_t a, int32_t b) {
  constexpr uint32_t kQInv = 58728449;
  constexpr int64_t kModulus = 8380417;
  int64_t product = int64_t(a) * b;
  uint32_t inverted = uint32_t(uint64_t(product) * kQInv);
  int64_t difference = product - signed32(inverted) * kModulus;
  return int32_t(difference >> 32);
}

int32_t montgomery_reduce_k(int32_t a, int32_t b) {
	constexpr uint32_t kQInv = 62209;
	constexpr int32_t kModulus = 3329;
	int32_t product = a * b;
	uint32_t inverted =
		((static_cast<uint32_t>(product) & 0xffffu) * kQInv) & 0xffffu;
	int32_t factor = signed16(inverted);
	int32_t difference = product - factor * kModulus;
	return signed16(static_cast<uint32_t>(difference) >> 16);
}

int32_t barrett_reduce_k(int32_t value) {
	constexpr int32_t kMultiplier = 20159;
	constexpr int32_t kRounding = 1 << 25;
	constexpr int32_t kModulus = 3329;
	int32_t rounded = kMultiplier * value + kRounding;
	int32_t quotient = rounded >> 26;
	return signed16(value - quotient * kModulus);
}

uint32_t ntt_beats(const instr_trace_t* trace) {
  constexpr uint32_t lanes = VX_CFG_NUM_ALU_LANES;
  constexpr uint32_t multipliers = std::min(lanes, uint32_t(VX_CFG_NTT_MUL_LANES));
  static_assert(VX_CFG_NTT_MUL_LANES > 0 && multipliers <= (lanes + 1) / 2
                && lanes % multipliers == 0, "invalid NTT multiplier count");
  auto type = std::get<NttType>(trace->op_type);
  auto products = (type == NttType::MUL_K || type == NttType::BF_GS_K
                || type == NttType::MUL_D) ? lanes : (lanes + 1) / 2;
  return products / multipliers;
}

}
#endif

AluUnit::AluUnit(const SimContext& ctx, const char* name, Core* core)
	: FuncUnit<VX_CFG_NUM_ALU_BLOCKS>(ctx, name, core)
#if defined(VX_CFG_EXT_KSG25_ENABLE) || defined(VX_CFG_EXT_KROUND25_ENABLE)
  , DispatchRelease(make_sim_channels<uint32_t, VX_CFG_NUM_ALU_BLOCKS>(this))
  , int_results_(make_sim_channels<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS>(this, 1))
#ifdef VX_CFG_EXT_M_ENABLE
  , mdv_results_(make_sim_channels<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS>(this, 1))
#endif
#ifdef VX_CFG_EXT_KSG25_ENABLE
  , ksg25_results_(make_sim_channels<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS>(this, 2))
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
  , kround25_results_(make_sim_channels<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS>(this, 4))
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
  , ntt_results_(make_sim_channels<instr_trace_t*, VX_CFG_NUM_ALU_BLOCKS>(this, 1))
#endif
#endif
{}

void AluUnit::on_reset() {
#if defined(VX_CFG_EXT_KSG25_ENABLE) || defined(VX_CFG_EXT_KROUND25_ENABLE)
  next_pe_.fill(0);
#ifdef VX_CFG_EXT_NTT_ENABLE
  ntt_pipeline_.fill({});
  ntt_pending_.fill(nullptr);
  ntt_remaining_.fill(0);
#endif
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
  ntt_ready_cycle_.fill(0);
#endif
}

uint32_t AluUnit::latency_of(const instr_trace_t* trace) const {
#ifdef VX_CFG_EXT_KSG25_ENABLE
  if (std::get_if<Ksg25Type>(&trace->op_type)) {
    return 3;
  }
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
  if (std::get_if<Kround25Type>(&trace->op_type)) {
    return 5;
  }
#endif
	if (std::get_if<AluType>(&trace->op_type)) {
		auto alu_type = std::get<AluType>(trace->op_type);
		switch (alu_type) {
		case AluType::LUI:
		case AluType::AUIPC:
		case AluType::ADD:
		case AluType::SUB:
		case AluType::SLL:
		case AluType::SRL:
		case AluType::SRA:
		case AluType::SLT:
		case AluType::SLTU:
		case AluType::XOR:
		case AluType::AND:
		case AluType::OR:
		case AluType::CZERO:
			return 2;
		default:
			std::abort();
		}
	} else if (std::get_if<VoteType>(&trace->op_type)) {
		return 2;
	} else if (std::get_if<ShflType>(&trace->op_type)) {
		return 2;
	} else if (std::get_if<WgatherType>(&trace->op_type)) {
		return 2;
	} else if (std::get_if<BrType>(&trace->op_type)) {
		auto br_type = std::get<BrType>(trace->op_type);
		switch (br_type) {
		case BrType::BR:
		case BrType::JAL:
		case BrType::JALR:
		case BrType::SYS:
			return 2;
		default:
			std::abort();
		}
	} else if (std::get_if<MdvType>(&trace->op_type)) {
		auto mdv_type = std::get<MdvType>(trace->op_type);
		switch (mdv_type) {
		case MdvType::MUL:
		case MdvType::MULHU:
		case MdvType::MULH:
		case MdvType::MULHSU:
			return 2;
		case MdvType::DIV:
		case MdvType::DIVU:
		case MdvType::REM:
		case MdvType::REMU:
			// Simulation divides are fully pipelined at the multiplier's
			// depth, not iterative.
			return 2;
		default:
			std::abort();
		}
#ifdef VX_CFG_EXT_NTT_ENABLE
	} else if (std::get_if<NttType>(&trace->op_type)) {
		return 6 + ntt_beats(trace);
#endif
	}
	std::abort();
}

void AluUnit::execute(instr_trace_t* trace) {
#ifdef VX_CFG_EXT_KROUND25_ENABLE
  if (auto type = std::get_if<Kround25Type>(&trace->op_type)) {
    if (trace->tmask.size() != 32 || !trace->tmask.all()) {
      std::cerr << *type << " requires a full 32-lane mask, wid="
                << trace->wid << ", PC=0x" << std::hex << trace->PC
                << std::dec << ", mask=" << trace->tmask << std::endl;
      std::abort();
    }
    static const uint8_t rho[25] = {
      0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
      25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
    };
    static const uint64_t round_constants[24] = {
      0x0000000000000001ULL, 0x0000000000008082ULL,
      0x800000000000808aULL, 0x8000000080008000ULL,
      0x000000000000808bULL, 0x0000000080000001ULL,
      0x8000000080008081ULL, 0x8000000000008009ULL,
      0x000000000000008aULL, 0x0000000000000088ULL,
      0x0000000080008009ULL, 0x000000008000000aULL,
      0x000000008000808bULL, 0x800000000000008bULL,
      0x8000000000008089ULL, 0x8000000000008003ULL,
      0x8000000000008002ULL, 0x8000000000000080ULL,
      0x000000000000800aULL, 0x800000008000000aULL,
      0x8000000080008081ULL, 0x8000000000008080ULL,
      0x0000000080000001ULL, 0x8000000080008008ULL,
    };
    uint32_t round = std::get<IntrAluArgs>(trace->instr_ptr->get_args()).imm;
    if (round >= 24) {
      std::cerr << *type << " requires a round in 0..23" << std::endl;
      std::abort();
    }
    std::array<uint64_t, 25> state{}, rhopi{};
    std::array<uint64_t, 5> parity{};
    for (uint32_t t = 0; t < 25; ++t) {
#if (VX_CFG_XLEN == 64)
      state[t] = trace->src_data[0][t].u64;
#else
      state[t] = (uint64_t(trace->src_data[1][t].u32) << 32)
               | trace->src_data[0][t].u32;
#endif
      parity[t % 5] ^= state[t];
    }
    for (uint32_t t = 0; t < 25; ++t) {
      uint32_t x = t % 5;
      uint64_t next = parity[(x + 1) % 5];
      state[t] ^= parity[(x + 4) % 5];
      state[t] ^= (next << 1) | (next >> 63);
    }
    for (uint32_t t = 0; t < 25; ++t) {
      uint32_t x = t % 5, y = t / 5;
      uint32_t dst = y + 5 * ((2 * x + 3 * y) % 5);
      uint32_t shift = rho[t];
      rhopi[dst] = (state[t] << shift) | (state[t] >> ((64 - shift) & 63));
    }
    trace->dst_data.assign(VX_CFG_NUM_THREADS, reg_data_t{});
    for (uint32_t t = 0; t < 25; ++t) {
      uint32_t x = t % 5, row = 5 * (t / 5);
      uint64_t value = rhopi[t]
                     ^ (~rhopi[row + (x + 1) % 5] & rhopi[row + (x + 2) % 5]);
      if (t == 0) {
        value ^= round_constants[round];
      }
#if (VX_CFG_XLEN == 64)
      trace->dst_data[t].u64 = value;
#else
      trace->dst_data[t].u32 = (*type == Kround25Type::ROUND_L)
                            ? uint32_t(value) : uint32_t(value >> 32);
#endif
    }
    DT(3, this->name() << " execute: op=" << *type << ", " << *trace);
    return;
  }
#endif
#ifdef VX_CFG_EXT_KSG25_ENABLE
  if (auto type = std::get_if<Ksg25Type>(&trace->op_type)) {
    if (trace->tmask.size() != 32 || !trace->tmask.all()) {
      std::cerr << *type << " requires a full 32-lane mask, wid="
                << trace->wid << ", PC=0x" << std::hex << trace->PC
                << std::dec << ", mask=" << trace->tmask << std::endl;
      std::abort();
    }
    const auto& state_in = trace->src_data[0];
    const auto& aux_in = trace->src_data[1];
    if (*type == Ksg25Type::RHOPI_L || *type == Ksg25Type::RHOPI_H) {
      static const uint8_t rho[25] = {
        0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
        25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
      };
      trace->dst_data.assign(VX_CFG_NUM_THREADS, reg_data_t{});
      for (uint32_t t = 0; t < 25; ++t) {
        uint32_t source = ((t % 5 + 3 * (t / 5)) % 5) + 5 * (t % 5);
#if (VX_CFG_XLEN == 64)
        uint64_t word = state_in[source].u64;
#else
        uint64_t word = (uint64_t(aux_in[source].u32) << 32) | state_in[source].u32;
#endif
        uint32_t shift = rho[source];
        uint64_t value = (word << shift) | (word >> ((64 - shift) & 63));
#if (VX_CFG_XLEN == 64)
        trace->dst_data[t].u64 = value;
#else
        trace->dst_data[t].u32 = (*type == Ksg25Type::RHOPI_L)
                              ? uint32_t(value) : uint32_t(value >> 32);
#endif
      }
      return;
    }
    if (*type == Ksg25Type::CHII_L || *type == Ksg25Type::CHII_H) {
      static const uint64_t round_constants[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL,
        0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL,
        0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL,
        0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL,
        0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL,
        0x0000000080000001ULL, 0x8000000080008008ULL,
      };
      uint32_t round = aux_in[0].u32;
      for (uint32_t t = 0; t < 32; ++t) {
        if (round >= 24 || aux_in[t].u32 != round) {
          std::cerr << *type << " requires a uniform round in 0..23" << std::endl;
          std::abort();
        }
      }
      trace->dst_data.assign(VX_CFG_NUM_THREADS, reg_data_t{});
      for (uint32_t t = 0; t < 25; ++t) {
        uint32_t row = 5 * (t / 5), x = t % 5;
#if (VX_CFG_XLEN == 64)
        trace->dst_data[t].u64 = state_in[t].u64
          ^ (~state_in[row + (x+1)%5].u64 & state_in[row + (x+2)%5].u64);
#else
        trace->dst_data[t].u32 = state_in[t].u32
          ^ (~state_in[row + (x+1)%5].u32 & state_in[row + (x+2)%5].u32);
#endif
      }
#if (VX_CFG_XLEN == 64)
      trace->dst_data[0].u64 ^= round_constants[round];
#else
      trace->dst_data[0].u32 ^= (*type == Ksg25Type::CHII_L)
                            ? uint32_t(round_constants[round]) : uint32_t(round_constants[round] >> 32);
#endif
      return;
    }
    std::array<uint64_t, 5> parity{};
    for (uint32_t t = 0; t < 25; ++t) {
#if (VX_CFG_XLEN == 64)
      parity[t % 5] ^= state_in[t].u64;
#else
      parity[t % 5] ^= (uint64_t(aux_in[t].u32) << 32) | state_in[t].u32;
#endif
    }
    trace->dst_data.assign(VX_CFG_NUM_THREADS, reg_data_t{});
    for (uint32_t t = 0; t < 25; ++t) {
      uint32_t x = t % 5;
      uint64_t next = parity[(x + 1) % 5];
#if (VX_CFG_XLEN == 64)
      uint64_t state = state_in[t].u64;
#else
      uint64_t state = (uint64_t(aux_in[t].u32) << 32) | state_in[t].u32;
#endif
      uint64_t value = state ^ parity[(x + 4) % 5]
                    ^ ((next << 1) | (next >> 63));
#if (VX_CFG_XLEN == 64)
      trace->dst_data[t].u64 = value;
#else
      trace->dst_data[t].u32 = (*type == Ksg25Type::THETA_L)
                            ? uint32_t(value) : uint32_t(value >> 32);
#endif
    }
    DT(3, this->name() << " execute: op=" << *type << ", " << *trace);
    return;
  }
#endif
	auto& sched = core_->scheduler();
	auto& warp = sched.warp(trace->wid);
	// Use trace->tmask captured at issue for per-thread active checks.
	// `warp.tmask` is the live warp state and may change due to divergent
	// control flow before this trace executes; the rest of the pipeline
	// (commit, writeback) keys off trace->tmask, so a mismatch leaves lanes
	// with stale dst_data. See LsuUnit::execute for the same pattern.
	auto& tmask = trace->tmask;
	auto& instr = *trace->instr_ptr;
	auto instrArgs = instr.get_args();
	uint32_t num_threads = VX_CFG_NUM_THREADS;
	auto& rs1_data = trace->src_data[0];
	auto& rs2_data = trace->src_data[1];
	auto& rs3_data = trace->src_data[2];

	// derive thread bounds from operand mask
	uint32_t thread_start = 0;
	for (; thread_start < num_threads; ++thread_start) {
		if (tmask.test(thread_start))
			break;
	}
	int32_t thread_last = num_threads - 1;
	for (; thread_last >= 0; --thread_last) {
		if (tmask.test(thread_last))
			break;
	}

	bool is_w_enabled = false;
#ifdef VX_CFG_XLEN_64
	is_w_enabled = true;
#endif

	// always size dst_data so per-op compute can write unconditionally;
	// commit_writeback gates regfile actual write on wb (skipped for x0 dst).
	trace->dst_data.assign(num_threads, reg_data_t{});
	auto& rd_data = trace->dst_data;

	if (std::get_if<AluType>(&trace->op_type)) {
		auto alu_type = std::get<AluType>(trace->op_type);
		auto aluArgs = std::get<IntrAluArgs>(instrArgs);
		Word imm = sext<Word>(aluArgs.imm, 32);
		switch (alu_type) {
		case AluType::LUI: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = imm;
			}
		} break;
		case AluType::AUIPC: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = imm + trace->PC;
			}
		} break;
		case AluType::ADD: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && aluArgs.is_w) {
					auto result = rs1_data[t].i32 + (int32_t)(aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32);
					rd_data[t].i = sext((uint64_t)result, 32);
				} else {
					rd_data[t].i = rs1_data[t].i + (aluArgs.is_imm ? imm : rs2_data[t].i);
				}
			}
		} break;
		case AluType::SUB: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && aluArgs.is_w) {
					auto result = rs1_data[t].i32 - (int32_t)(aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32);
					rd_data[t].i = sext((uint64_t)result, 32);
				} else {
					rd_data[t].i = rs1_data[t].i - (aluArgs.is_imm ? imm : rs2_data[t].i);
				}
			}
		} break;
		case AluType::SLT: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = rs1_data[t].i < (aluArgs.is_imm ? WordI(imm) : rs2_data[t].i);
			}
		} break;
		case AluType::SLTU: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = rs1_data[t].u < (aluArgs.is_imm ? imm : rs2_data[t].u);
			}
		} break;
		case AluType::SLL: {
			Word shamt_mask = (Word(1) << log2up(VX_CFG_XLEN)) - 1;
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && aluArgs.is_w) {
					uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
					uint32_t result = (uint32_t)rs1_data[t].i << shamt;
					rd_data[t].i = sext((uint64_t)result, 32);
				} else {
					Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
					rd_data[t].i = rs1_data[t].i << shamt;
				}
			}
		} break;
		case AluType::SRA: {
			Word shamt_mask = (Word(1) << log2up(VX_CFG_XLEN)) - 1;
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && aluArgs.is_w) {
					uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
					uint32_t result = (int32_t)rs1_data[t].i >> shamt;
					rd_data[t].i = sext((uint64_t)result, 32);
				} else {
					Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
					rd_data[t].i = rs1_data[t].i >> shamt;
				}
			}
		} break;
		case AluType::SRL: {
			Word shamt_mask = (Word(1) << log2up(VX_CFG_XLEN)) - 1;
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && aluArgs.is_w) {
					uint32_t shamt = (aluArgs.is_imm ? aluArgs.imm : rs2_data[t].i32) & shamt_mask;
					uint32_t result = (uint32_t)rs1_data[t].i >> shamt;
					rd_data[t].i = sext((uint64_t)result, 32);
				} else {
					Word shamt = (aluArgs.is_imm ? imm : rs2_data[t].i) & shamt_mask;
					rd_data[t].i = rs1_data[t].u >> shamt;
				}
			}
		} break;
		case AluType::AND: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = rs1_data[t].i & (aluArgs.is_imm ? imm : rs2_data[t].i);
			}
		} break;
		case AluType::OR: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = rs1_data[t].i | (aluArgs.is_imm ? imm : rs2_data[t].i);
			}
		} break;
		case AluType::XOR: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = rs1_data[t].i ^ (aluArgs.is_imm ? imm : rs2_data[t].i);
			}
		} break;
		case AluType::CZERO: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				bool cond = (rs2_data[t].i == 0) ^ aluArgs.imm;
				rd_data[t].i = cond ? 0 : rs1_data[t].i;
			}
		} break;
		default:
			std::abort();
		}
		DT(3, this->name() << " execute: op=" << alu_type << ", " << *trace);
	} else if (std::get_if<VoteType>(&trace->op_type)) {
		auto vote_type = std::get<VoteType>(trace->op_type);
		bool has_vote_true = false;
		bool has_vote_false = false;
		Word ballot = 0;
		for (uint32_t t = thread_start; t < num_threads; ++t) {
			if (!tmask.test(t)) continue;
			auto is_pred = rs1_data[t].i & 0x1;
			if (is_pred) {
				has_vote_true = true;
				ballot |= (Word(1) << t);
			} else {
				has_vote_false = true;
			}
		}
		for (uint32_t t = thread_start; t < num_threads; ++t) {
			switch (vote_type) {
			case VoteType::ALL: rd_data[t].i = !has_vote_false; break;
			case VoteType::ANY: rd_data[t].i = has_vote_true; break;
			case VoteType::UNI: rd_data[t].i = !has_vote_true || !has_vote_false; break;
			case VoteType::BAL: rd_data[t].i = ballot; break;
			default: std::abort();
			}
		}
	} else if (std::get_if<ShflType>(&trace->op_type)) {
		// The permute network spans one SIMD group, not the warp: a warp wider than
		// the datapath executes as NUM_THREADS/NUM_ALU_LANES back-to-back groups,
		// each with its own register-file row, and a shuffle cannot cross a group.
		// The lane-select operands are therefore sliced to the group's lane width,
		// and a masked source lane yields the reader's own value.
		auto shfl_type = std::get<ShflType>(trace->op_type);
		const uint32_t lanes = VX_CFG_NUM_ALU_LANES;
		const int lane_mask = (int)lanes - 1;
		for (uint32_t t = thread_start; t < num_threads; ++t) {
			if (!tmask.test(t)) continue;
			uint32_t base = (t / lanes) * lanes;
			int i = (int)(t - base);
			auto bc  = rs2_data[t].i;
			int bval = (bc >>  0) & lane_mask;
			int cval = (bc >>  6) & lane_mask;
			int mask = (bc >> 12) & lane_mask;
			int minLane = i & mask;
			int maxLane = minLane | (cval & ~mask);
			int lane = i;
			switch (shfl_type) {
			case ShflType::UP: {
				int up = i - bval;
				if (up >= minLane) {
					lane = up;
				}
				break;
			}
			case ShflType::DOWN: {
				int down = i + bval;
				if (down <= maxLane) {
					lane = down;
				}
				break;
			}
			case ShflType::BFLY: {
				int bfly = i ^ bval;
				if (bfly <= maxLane) {
					lane = bfly;
				}
				break;
			}
			case ShflType::IDX: {
				int idx = minLane | (bval & ~mask);
				if (idx <= maxLane) {
					lane = idx;
				}
				break;
			}
			default: std::abort();
			}
			uint32_t src = base + (uint32_t)lane;
			rd_data[t].i = tmask.test(src) ? rs1_data[src].i : rs1_data[t].i;
		}
	} else if (std::get_if<WgatherType>(&trace->op_type)) {
		// Each group of 4 lanes operates independently; the nominal source lane
		// (group_base + src_offset) is suppressed by clearing its tmask bit so
		// the standard writeback path skips it (regfile keeps its prior value).
		// When that nominal lane is masked (partial warp), the read falls back
		// to the last active lane — mirrors VX_alu_int.sv's last_tid select.
		auto wgArgs = std::get<IntrWgatherArgs>(instrArgs);
		uint32_t src_offset = wgArgs.src_lane;
		// Snapshot the issue-time active mask: the loop below mutates trace->tmask
		// (which `tmask` aliases) to suppress source lanes, so source-lane liveness
		// must be judged against the pre-suppression mask, not the live one.
		auto active = tmask;
		uint32_t last_tid = thread_start;
		for (uint32_t t = thread_start; t < num_threads; ++t)
			if (active.test(t)) last_tid = t;
		// WGATHER writes the FULL nibble (every non-source lane) regardless of
		// the active mask, so the gathered value is materialised even in masked
		// lanes; source lanes stay suppressed (keep their self value). Reads fall
		// back to the last active lane when the nominal source is masked.
		for (uint32_t t = thread_start; t < num_threads; ++t) {
			if ((t & 0x3u) == src_offset) {
				trace->tmask.reset(t); // suppress writeback for source lane
				continue;
			}
			trace->tmask.set(t);       // force-write non-source lane
			uint32_t nominal = (t & ~0x3u) + src_offset;
			uint32_t sl      = active.test(nominal) ? nominal : last_tid;
			uint32_t offset  = (t - nominal) & 0x3u;
			if      (offset == 1) rd_data[t].i = rs1_data[sl].i;
			else if (offset == 2) rd_data[t].i = rs2_data[sl].i;
			else if (offset == 3) rd_data[t].i = rs3_data[sl].i;
		}
	} else if (std::get_if<BrType>(&trace->op_type)) {
		auto br_type = std::get<BrType>(trace->op_type);
		auto brArgs = std::get<IntrBrArgs>(instrArgs);
		Word offset = sext<Word>(brArgs.offset, 32);
		switch (br_type) {
		case BrType::BR: {
			bool curr_taken = false;
			uint32_t t = static_cast<uint32_t>(thread_last);
			switch (brArgs.cmp) {
			case 0: curr_taken = (rs1_data[t].i == rs2_data[t].i); break;
			case 1: curr_taken = (rs1_data[t].i != rs2_data[t].i); break;
			case 4: curr_taken = (rs1_data[t].i <  rs2_data[t].i); break;
			case 5: curr_taken = (rs1_data[t].i >= rs2_data[t].i); break;
			case 6: curr_taken = (rs1_data[t].u <  rs2_data[t].u); break;
			case 7: curr_taken = (rs1_data[t].u >= rs2_data[t].u); break;
			default: std::abort();
			}
			if (curr_taken) {
				warp.PC = trace->PC + offset;
			}
			core_->perf_stats().branches += 1;
		} break;
		case BrType::JAL: {
			// RVC source returns PC+2; full RV32I returns PC+4.
			Word link_pc = trace->PC + (brArgs.is_rvc ? 2 : 4);
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = link_pc;
			}
			warp.PC = trace->PC + offset;
			core_->perf_stats().branches += 1;
		} break;
		case BrType::JALR: {
			// RVC source returns PC+2; full RV32I returns PC+4.
			Word link_pc = trace->PC + (brArgs.is_rvc ? 2 : 4);
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = link_pc;
			}
			// JALR clears bit 0 of the computed target (RISC-V ISA); the RTL
			// release build drops it implicitly via from_fullPC() truncation.
			warp.PC = (rs1_data[thread_last].u + offset) & ~Word(1);
			core_->perf_stats().branches += 1;
		} break;
		case BrType::SYS:
			switch (brArgs.offset) {
			// ECALL / EBREAK: synchronous trap — redirect warp PC to mtvec.
			case 0x000: sched.trigger_ecall(trace->wid, trace->PC);  break;
			case 0x001: sched.trigger_ebreak(trace->wid, trace->PC); break;
			// URET / SRET / MRET: trap return — restore warp PC from mepc.
			case 0x002: case 0x102: case 0x302: sched.mret(trace->wid); break;
			default: std::abort();
			}
			core_->perf_stats().branches += 1;
			break;
		default:
			std::abort();
		}
		DT(3, this->name() << " execute: op=" << br_type << ", " << *trace);
	} else if (std::get_if<MdvType>(&trace->op_type)) {
		auto mdv_type = std::get<MdvType>(trace->op_type);
		auto mdvArgs = std::get<IntrMdvArgs>(instrArgs);
		switch (mdv_type) {
		case MdvType::MUL: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && mdvArgs.is_w) {
					auto product = rs1_data[t].i32 * rs2_data[t].i32;
					rd_data[t].i = sext((uint64_t)product, 32);
				} else {
					rd_data[t].i = rs1_data[t].i * rs2_data[t].i;
				}
			}
		} break;
		case MdvType::MULH: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				auto first = static_cast<DWordI>(rs1_data[t].i);
				auto second = static_cast<DWordI>(rs2_data[t].i);
				rd_data[t].i = (first * second) >> VX_CFG_XLEN;
			}
		} break;
		case MdvType::MULHSU: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				auto first = static_cast<DWordI>(rs1_data[t].i);
				auto second = static_cast<DWord>(rs2_data[t].u);
				rd_data[t].i = (first * second) >> VX_CFG_XLEN;
			}
		} break;
		case MdvType::MULHU: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				auto first = static_cast<DWord>(rs1_data[t].u);
				auto second = static_cast<DWord>(rs2_data[t].u);
				rd_data[t].i = (first * second) >> VX_CFG_XLEN;
			}
		} break;
		case MdvType::DIV: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && mdvArgs.is_w) {
					auto dividen = rs1_data[t].i32;
					auto divisor = rs2_data[t].i32;
					int32_t largest_negative = 0x80000000;
					int32_t quotient;
					if (divisor == 0)                                   quotient = -1;
					else if (dividen == largest_negative && divisor == -1) quotient = dividen;
					else                                                quotient = dividen / divisor;
					rd_data[t].i = sext((uint64_t)quotient, 32);
				} else {
					auto dividen = rs1_data[t].i;
					auto divisor = rs2_data[t].i;
					auto largest_negative = WordI(1) << (VX_CFG_XLEN-1);
					WordI quotient;
					if (divisor == 0)                                   quotient = -1;
					else if (dividen == largest_negative && divisor == -1) quotient = dividen;
					else                                                quotient = dividen / divisor;
					rd_data[t].i = quotient;
				}
			}
		} break;
		case MdvType::DIVU: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && mdvArgs.is_w) {
					auto dividen = rs1_data[t].u32;
					auto divisor = rs2_data[t].u32;
					uint32_t quotient;
					if (divisor != 0) quotient = dividen / divisor;
					else              quotient = -1;
					rd_data[t].i = sext((uint64_t)quotient, 32);
				} else {
					auto dividen = rs1_data[t].u;
					auto divisor = rs2_data[t].u;
					Word quotient;
					if (divisor != 0) quotient = dividen / divisor;
					else              quotient = -1;
					rd_data[t].i = quotient;
				}
			}
		} break;
		case MdvType::REM: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && mdvArgs.is_w) {
					auto dividen = rs1_data[t].i32;
					auto divisor = rs2_data[t].i32;
					int32_t largest_negative = 0x80000000;
					int32_t remainder;
					if (divisor == 0)                                   remainder = dividen;
					else if (dividen == largest_negative && divisor == -1) remainder = 0;
					else                                                remainder = dividen % divisor;
					rd_data[t].i = sext((uint64_t)remainder, 32);
				} else {
					auto dividen = rs1_data[t].i;
					auto divisor = rs2_data[t].i;
					auto largest_negative = WordI(1) << (VX_CFG_XLEN-1);
					WordI remainder;
					if (rs2_data[t].i == 0)                             remainder = dividen;
					else if (dividen == largest_negative && divisor == -1) remainder = 0;
					else                                                remainder = dividen % divisor;
					rd_data[t].i = remainder;
				}
			}
		} break;
		case MdvType::REMU: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				if (is_w_enabled && mdvArgs.is_w) {
					auto dividen = (uint32_t)rs1_data[t].u32;
					auto divisor = (uint32_t)rs2_data[t].u32;
					uint32_t remainder;
					if (divisor != 0) remainder = dividen % divisor;
					else              remainder = dividen;
					rd_data[t].i = sext((uint64_t)remainder, 32);
				} else {
					auto dividen = rs1_data[t].u;
					auto divisor = rs2_data[t].u;
					Word remainder;
					if (rs2_data[t].i != 0) remainder = dividen % divisor;
					else                    remainder = dividen;
					rd_data[t].i = remainder;
				}
			}
		} break;
		default:
			std::abort();
		}
		DT(3, this->name() << " execute: op=" << mdv_type << ", " << *trace);
#ifdef VX_CFG_EXT_NTT_ENABLE
	} else if (std::get_if<NttType>(&trace->op_type)) {
		auto ntt_type = std::get<NttType>(trace->op_type);
		switch (ntt_type) {
		case NttType::MUL_K: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				int32_t a = signed16(rs1_data[t].u);
				int32_t b = signed16(rs2_data[t].u);
				rd_data[t].i = static_cast<WordI>(montgomery_reduce_k(a, b));
			}
		} break;
		case NttType::MUL_D: {
			for (uint32_t t = thread_start; t < num_threads; ++t) {
				if (!tmask.test(t)) continue;
				rd_data[t].i = static_cast<WordI>(montgomery_reduce_d(
				    int32_t(rs1_data[t].u), int32_t(rs2_data[t].u)));
			}
		} break;
		case NttType::BF_CT_K:
		case NttType::BF_GS_K: {
			auto nttArgs = std::get<IntrNttArgs>(instrArgs);
			const uint32_t lanes = VX_CFG_NUM_ALU_LANES;
			const uint32_t distance = 1u << nttArgs.stage;
			if (lanes != 32 || distance >= lanes || (num_threads % lanes) != 0)
				std::abort();
			for (uint32_t base = 0; base < num_threads; base += lanes) {
				for (uint32_t lo = 0; lo < lanes; ++lo) {
					if (lo & distance) continue;
					uint32_t low_lane = base + lo;
					uint32_t high_lane = low_lane + distance;
					bool low_active = tmask.test(low_lane);
					bool high_active = tmask.test(high_lane);
					if (low_active != high_active)
						std::abort();
					if (!low_active) continue;

					int32_t a = signed16(rs1_data[low_lane].u);
					int32_t b = signed16(rs1_data[high_lane].u);
					int32_t zeta = signed16(rs2_data[low_lane].u);
					if (ntt_type == NttType::BF_CT_K) {
						int32_t product = montgomery_reduce_k(b, zeta);
						rd_data[low_lane].i = static_cast<WordI>(signed16(a + product));
						rd_data[high_lane].i = static_cast<WordI>(signed16(a - product));
					} else {
						int32_t sum = signed16(a + b);
						int32_t difference = signed16(b - a);
						rd_data[low_lane].i = static_cast<WordI>(barrett_reduce_k(sum));
						rd_data[high_lane].i =
							static_cast<WordI>(montgomery_reduce_k(difference, zeta));
					}
				}
			}
		} break;
		case NttType::BF_CT_D:
		case NttType::BF_GS_D: {
			auto nttArgs = std::get<IntrNttArgs>(instrArgs);
			const uint32_t lanes = VX_CFG_NUM_ALU_LANES;
			const uint32_t distance = 1u << nttArgs.stage;
			if (lanes != 32 || distance >= lanes || (num_threads % lanes) != 0)
				std::abort();
			for (uint32_t base = 0; base < num_threads; base += lanes) {
				for (uint32_t lo = 0; lo < lanes; ++lo) {
					if (lo & distance) continue;
					uint32_t low_lane = base + lo;
					uint32_t high_lane = low_lane + distance;
					bool low_active = tmask.test(low_lane);
					bool high_active = tmask.test(high_lane);
					if (low_active != high_active) std::abort();
					if (!low_active) continue;
					int32_t a = int32_t(rs1_data[low_lane].u);
					int32_t b = int32_t(rs1_data[high_lane].u);
					int32_t zeta = int32_t(rs2_data[low_lane].u);
					if (ntt_type == NttType::BF_CT_D) {
						int32_t product = montgomery_reduce_d(b, zeta);
						rd_data[low_lane].i = static_cast<WordI>(a + product);
						rd_data[high_lane].i = static_cast<WordI>(a - product);
					} else {
						rd_data[low_lane].i = static_cast<WordI>(a + b);
						rd_data[high_lane].i = static_cast<WordI>(
						    montgomery_reduce_d(a - b, zeta));
					}
				}
			}
		} break;
		default:
			std::abort();
		}
		DT(3, this->name() << " execute: op=" << ntt_type << ", " << *trace);
#endif
	} else {
		std::abort();
	}
}

void AluUnit::on_tick() {
  bool idle = true;
#if defined(VX_CFG_EXT_NTT_ENABLE) && !defined(VX_CFG_EXT_KSG25_ENABLE) && !defined(VX_CFG_EXT_KROUND25_ENABLE)
  const auto cycle = SimPlatform::instance().cycles();
#endif
  for (uint32_t b = 0; b < VX_CFG_NUM_ALU_BLOCKS; ++b) {
    auto& input = Inputs.at(b);
#if defined(VX_CFG_EXT_KSG25_ENABLE) || defined(VX_CFG_EXT_KROUND25_ENABLE)
    std::array<SimChannel<instr_trace_t*>*, kNumPEs> results = {
      &int_results_[b],
#ifdef VX_CFG_EXT_M_ENABLE
      &mdv_results_[b],
#endif
#ifdef VX_CFG_EXT_KSG25_ENABLE
      &ksg25_results_[b],
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
      &kround25_results_[b],
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
      &ntt_results_[b],
#endif
    };
    auto& output = Outputs.at(b);
    if (!output.full()) {
      for (uint32_t i = 0; i < kNumPEs; ++i) {
        uint32_t pe = (next_pe_[b] + i) % kNumPEs;
        if (!results[pe]->empty()) {
          output.send(results[pe]->peek(), 1);
          results[pe]->pop();
          next_pe_[b] = (pe + 1) % kNumPEs;
          break;
        }
      }
    }
#ifdef VX_CFG_EXT_NTT_ENABLE
    const bool ntt_advance = !ntt_results_[b].full();
    instr_trace_t* ntt_accepted = nullptr;
#endif
    if (!input.empty()) {
      auto trace = input.peek();
      uint32_t pe = 0;
      bool unit_ready = true;
#ifdef VX_CFG_EXT_M_ENABLE
      if (std::get_if<MdvType>(&trace->op_type)) {
        pe = 1;
      }
#endif
#ifdef VX_CFG_EXT_KSG25_ENABLE
      if (std::get_if<Ksg25Type>(&trace->op_type)) {
        pe = 1 + VX_CFG_EXT_M_ENABLED;
      }
#endif
#ifdef VX_CFG_EXT_KROUND25_ENABLE
      if (std::get_if<Kround25Type>(&trace->op_type)) {
        pe = 1 + VX_CFG_EXT_M_ENABLED + VX_CFG_EXT_KSG25_ENABLED;
      }
#endif
#ifdef VX_CFG_EXT_NTT_ENABLE
      if (std::get_if<NttType>(&trace->op_type)) {
        pe = 1 + VX_CFG_EXT_M_ENABLED + VX_CFG_EXT_KSG25_ENABLED
               + VX_CFG_EXT_KROUND25_ENABLED;
        unit_ready = ntt_advance && !ntt_pending_[b];
      }
#endif
      if (unit_ready && !results[pe]->full() && !DispatchRelease.at(b).full()) {
        this->execute(trace);
#ifdef VX_CFG_EXT_NTT_ENABLE
        if (std::get_if<NttType>(&trace->op_type)) {
          ntt_accepted = trace;
        } else
#endif
        {
          results[pe]->send(trace, this->latency_of(trace) - 1);
        }
        // Acceptance feeds back within the same core clock domain.
        DispatchRelease.at(b).send(trace->wid, 0);
        input.pop();
      }
    }
#ifdef VX_CFG_EXT_NTT_ENABLE
    auto& pipeline = ntt_pipeline_[b];
    if (ntt_advance) {
      // The final registered stage is the result channel; its stall freezes every beat.
      const auto& last = pipeline.back();
      if (last.trace && last.last) {
        ntt_results_[b].send(last.trace, 1);
      }
      for (uint32_t i = pipeline.size() - 1; i > 0; --i) {
        pipeline[i] = pipeline[i - 1];
      }
      if (ntt_pending_[b]) {
        bool last = --ntt_remaining_[b] == 0;
        pipeline[0] = {ntt_pending_[b], last};
        if (last) {
          ntt_pending_[b] = nullptr;
        }
      } else if (ntt_accepted) {
        ntt_remaining_[b] = ntt_beats(ntt_accepted) - 1;
        pipeline[0] = {ntt_accepted, ntt_remaining_[b] == 0};
        ntt_pending_[b] = ntt_remaining_[b] ? ntt_accepted : nullptr;
      } else {
        pipeline[0] = {};
      }
    }
    for (const auto& beat : pipeline) {
      idle &= (beat.trace == nullptr);
    }
    idle &= (ntt_pending_[b] == nullptr);
#endif
    for (auto result : results) {
      idle &= (result->size() == 0);
    }
#else
    if (!input.empty()) {
      auto& output = Outputs.at(b);
      if (!output.full()) {
        auto trace = input.peek();
#ifdef VX_CFG_EXT_NTT_ENABLE
        if (std::get_if<NttType>(&trace->op_type)
         && cycle < ntt_ready_cycle_.at(b)) {
          idle = false;
          continue;
        }
#endif
        this->execute(trace);
        uint32_t delay = this->latency_of(trace);
        output.send(trace, delay);
        input.pop();
#ifdef VX_CFG_EXT_NTT_ENABLE
        if (std::get_if<NttType>(&trace->op_type)) {
          ntt_ready_cycle_.at(b) = cycle + ntt_beats(trace);
        }
#endif
      }
    }
#endif
    idle &= (input.size() == 0);
  }
  // Bank deadlines need no wakeup until another request arrives.
  if (idle) {
    this->tick_sleep();
  }
}
