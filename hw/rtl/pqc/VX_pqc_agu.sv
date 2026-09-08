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

`include "VX_define.vh"

`ifdef VX_CFG_EXT_PQC_ENABLE

// Address generator and sequencer for KECCAKF, the one PQC instruction.
// It is the RTL twin of sim/simx/pqc/pqc_unit.{h,cpp}; read that header first,
// because the two ordering hazards it documents are properties of the design
// and not of the model, and both are handled here as well.
//
// The instruction is R-type on CUSTOM0 (funct7=0x05, funct3=0) with rs1 holding
// a pointer to 25 uint64_t and rd = x0. Nothing about the 200-byte state passes
// through the register file, which is the whole reason there is one instruction
// and not a load/permute/store trio.
//
// SEQUENCE, per active lane and one lane at a time:
//   LOAD   read WORDS words into state_r
//   PERM   hand all 1600 bits to VX_pqc_keccak_f1600
//   STORE  write WORDS words back
//   DRAIN  read all WORDS words again and wait for the responses
// The drain is not decoration. A store gets no response on this client, so
// without it the instruction would retire with its writes still in flight and
// the program's next load -- which is exactly what every sponge round does --
// could pass them.
//
// IMPLICIT ACQUIRE. The mirror hazard, and the one that only appears above one
// warp: a store commits when the LSU dispatches it, while its data is still
// travelling, so this client can read a word the caller has already written.
// The guard is the scheduler's own per-warp pending counter, read exactly as
// VX_wctl_unit reads it for WSYNC. ALM_EMPTY is 1, so alm_empty means at most
// one instruction of that warp is in flight -- and since decode wstall'd the
// warp on this one, that one IS this one. It is therefore the same test SimX
// spells has_pending_instrs(wid), not a coarser stand-in, which is what keeps
// the two cycle counts together as the warp count rises.
//
// Applied ONCE per instruction, at the first beat: gating every lane would wait
// on this unit's own in-flight stores, which never produce a response.
//
// ONE ENGINE. VX_CFG_PQC_NUM_ENGINES is the per-core versus per-lane A/B and it
// is answered: per-core, at ENGINES=1. The sequencer above serves lanes
// serially, so a second engine inside the core would have no second client to
// feed; the replication study lives in the standalone synthesis wrapper, where
// VX_pqc_unit is instantiated at each setting and measured for area.

module VX_pqc_agu import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = `VX_CFG_NUM_SFU_LANES,
    parameter UNROLL    = 1
) (
    input wire clk,
    input wire reset,

    // SFU PE-style request/response interfaces (sfu_execute_t / sfu_result_t)
    VX_execute_if.slave     execute_if,
    VX_result_if.master     result_if,

    // Memory client connection (to VX_lsu_scheduler at VX_core).
    VX_lsu_sched_if.master  client_if,

    // Per-warp pipeline drain status, straight from the scheduler.
    input wire [`VX_CFG_NUM_WARPS-1:0] warp_pending_alm_empty,

    // Warp release. decode sets is_wstall on KECCAKF, and no existing unlock
    // path covers it -- completion is not a branch, a barrier or a warp-control
    // event -- so the warp is released through VX_sched_unlock_if, the port the
    // RTU raised for the same reason. The ack lets the SFU arbitrate when both
    // units are built in: this one simply waits in PQC_DONE until it is taken,
    // which costs nothing when it is the only raiser.
    output wire                    unlock_req,
    output wire [NW_WIDTH-1:0]     unlock_wid,
    input  wire                    unlock_ack
);
    `UNUSED_SPARAM (INSTANCE_ID)

    localparam MEM_LANES = `VX_CFG_NUM_LSU_LANES;
    localparam WORD_BITS = LSU_WORD_SIZE * 8;
    // 25 x 64 bits as client words: 50 at XLEN=32, 25 at XLEN=64.
    localparam WORDS     = 200 / LSU_WORD_SIZE;
    localparam WCNT_W    = $clog2(WORDS + 1);
    localparam WIDX_W    = $clog2(WORDS);
    localparam LANE_W    = `LOG2UP(NUM_LANES);
    localparam ADDR_SHIFT = `CLOG2(LSU_WORD_SIZE);

    `STATIC_ASSERT ((WORDS * WORD_BITS) == 1600, ("Keccak state must be 1600 bits"))
    `STATIC_ASSERT (LSU_CLIENT_TAG_WIDTH >= WIDX_W, ("LSU client tag cannot encode the word index"))

    typedef enum logic [2:0] {
        PQC_IDLE,
        PQC_ACQ,
        PQC_LOAD,
        PQC_PERM,
        PQC_STORE,
        PQC_DRAIN,
        PQC_DONE
    } pqc_state_e;

    pqc_state_e state_q;

    logic unlock_pending_r;
    logic result_sent_r;

    sfu_header_t                       header_r;
    logic [NUM_LANES-1:0]              pending_lanes_r;   // lanes still to serve
    logic [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] base_r;
    logic [LANE_W-1:0]                 lane_r;
    logic [WCNT_W-1:0]                 sent_r;
    logic [WCNT_W-1:0]                 recvd_r;
    logic [WORDS-1:0][WORD_BITS-1:0]   state_r;

    // ------------------------------------------------------------------
    // Lane selection. Serve the lowest set bit of pending_lanes_r.
    // ------------------------------------------------------------------
    wire [NUM_LANES-1:0] next_lane_onehot = pending_lanes_r & (~pending_lanes_r + 1'b1);
    wire                 has_next_lane    = (| pending_lanes_r);
    logic [LANE_W-1:0]   next_lane_idx;
    always_comb begin
        next_lane_idx = '0;
        for (int i = NUM_LANES - 1; i >= 0; --i) begin
            if (next_lane_onehot[i]) begin
                next_lane_idx = LANE_W'(i);
            end
        end
    end

    // ------------------------------------------------------------------
    // Engine
    // ------------------------------------------------------------------
    wire            pe_req_valid = (state_q == PQC_PERM);
    wire            pe_req_ready;
    wire            pe_rsp_valid;
    wire [1599:0]   pe_rsp_state;

    VX_pqc_unit #(
        .NUM_LANES (1),
        .UNROLL    (UNROLL)
    ) pqc_engine (
        .clk       (clk),
        .reset     (reset),
        .req_valid (pe_req_valid),
        .req_state (state_r),
        .req_ready (pe_req_ready),
        .rsp_valid (pe_rsp_valid),
        .rsp_state (pe_rsp_state),
        .rsp_ready (1'b1)
    );

    // ------------------------------------------------------------------
    // Memory request
    // ------------------------------------------------------------------
    wire is_load_phase  = (state_q == PQC_LOAD) || (state_q == PQC_DRAIN);
    wire is_store_phase = (state_q == PQC_STORE);
    // The acquire only gates the very first beat of the instruction; after
    // that this unit's own traffic keeps the scheduler busy by construction.
    wire req_allowed    = (is_load_phase || is_store_phase) && (sent_r < WCNT_W'(WORDS));

    wire [`VX_CFG_XLEN-1:0] base_addr = base_r[lane_r];
    `UNUSED_VAR (base_addr[ADDR_SHIFT-1:0])
    wire [LSU_ADDR_WIDTH-1:0] base_word_addr =
        LSU_ADDR_WIDTH'(base_addr[`VX_CFG_XLEN-1:ADDR_SHIFT]);

`ifdef VX_CFG_LMEM_ENABLE
    wire [LSU_ADDR_WIDTH-1:0] lmem_addr_lo =
        LSU_ADDR_WIDTH'(`VX_CFG_XLEN'(`VX_MEM_LMEM_BASE_ADDR) >> ADDR_SHIFT);
    wire [LSU_ADDR_WIDTH-1:0] lmem_addr_hi =
        LSU_ADDR_WIDTH'((`VX_CFG_XLEN'(`VX_MEM_LMEM_BASE_ADDR)
                       + `VX_CFG_XLEN'(1 << `VX_CFG_LMEM_LOG_SIZE)) >> ADDR_SHIFT);
    wire base_is_local = (base_word_addr >= lmem_addr_lo)
                      && (base_word_addr <  lmem_addr_hi);
`else
    wire base_is_local = 1'b0;
`endif

    // Words remaining in this phase, capped at the client's lane count. The
    // last beat of a 50-word state at 4 lanes is a 2-word beat.
    wire [WCNT_W-1:0] words_left = WCNT_W'(WORDS) - sent_r;

    lsu_req_data_t req_w;
    always_comb begin
        req_w      = '0;
        req_w.rw   = is_store_phase;
        req_w.mask = '0;
        for (int i = 0; i < MEM_LANES; ++i) begin
            automatic mem_bus_attr_t a;
            a = '0;
            a.is_addr_local = base_is_local;
            if (WCNT_W'(i) < words_left) begin
                req_w.mask[i] = 1'b1;
            end
            req_w.addr[i]   = base_word_addr + LSU_ADDR_WIDTH'(sent_r) + LSU_ADDR_WIDTH'(i);
            req_w.byteen[i] = {LSU_WORD_SIZE{1'b1}};
            req_w.data[i]   = state_r[WIDX_W'(sent_r + WCNT_W'(i))];
            req_w.attr[i]   = a;
        end
        // The beat's starting word index rides in the tag and comes back on the
        // response, so a response is matched to its words without assuming
        // responses arrive in issue order or in whole beats -- with a cache they
        // do neither.
        req_w.tag = '0;
        req_w.tag[WIDX_W-1:0] = WIDX_W'(sent_r);
    end

    wire req_fire = client_if.req_valid && client_if.req_ready;
    wire rsp_fire = client_if.rsp_valid && client_if.rsp_ready;

    // Words carried by this beat / this response.
    logic [WCNT_W-1:0] req_words;
    always_comb begin
        req_words = '0;
        for (int i = 0; i < MEM_LANES; ++i) begin
            if (req_w.mask[i]) req_words = req_words + WCNT_W'(1);
        end
    end
    logic [WCNT_W-1:0] rsp_words;
    always_comb begin
        rsp_words = '0;
        for (int i = 0; i < MEM_LANES; ++i) begin
            if (client_if.rsp_data.mask[i]) rsp_words = rsp_words + WCNT_W'(1);
        end
    end

    wire [WIDX_W-1:0] rsp_word_base = client_if.rsp_data.tag[WIDX_W-1:0];

    assign client_if.req_valid = req_allowed;
    assign client_if.req_data  = req_w;
    assign client_if.rsp_ready = is_load_phase;
    `UNUSED_VAR (client_if.rsp_data.sop)
    `UNUSED_VAR (client_if.rsp_data.eop)

    // ------------------------------------------------------------------
    // Sequencer
    // ------------------------------------------------------------------
    wire execute_fire = execute_if.valid && execute_if.ready;
    wire result_fire  = result_if.valid && result_if.ready;

    always @(posedge clk) begin
        if (reset) begin
            state_q         <= PQC_IDLE;
            unlock_pending_r <= 1'b0;
            result_sent_r    <= 1'b0;
            header_r        <= '0;
            pending_lanes_r <= '0;
            base_r          <= '0;
            lane_r          <= '0;
            sent_r          <= '0;
            recvd_r         <= '0;
            state_r         <= '0;
        end else begin
            if (unlock_req && unlock_ack) begin
                unlock_pending_r <= 1'b0;
            end
            case (state_q)
            PQC_IDLE: begin
                if (execute_fire) begin
                    header_r        <= execute_if.data.header;
                    pending_lanes_r <= execute_if.data.header.tmask;
                    base_r          <= execute_if.data.rs1_data;
                    sent_r          <= '0;
                    recvd_r         <= '0;
                    // No active lane: nothing to permute, retire immediately.
                    state_q <= (| execute_if.data.header.tmask) ? PQC_ACQ : PQC_DONE;
                    // ONLY THE LAST PASS RELEASES THE WARP. With
                    // SIMD_WIDTH < NUM_THREADS one KECCAKF arrives as several
                    // passes, each with its own tmask (ci/testcases/config1.yaml
                    // builds SIMD_WIDTH=1 and 2, so this is not hypothetical).
                    // Unlocking on the first would let the caller's next load
                    // run while the later passes' lanes are still unpermuted --
                    // the same stale read the acquire exists to prevent,
                    // reintroduced from the other end. Earlier passes still
                    // return their result normally; they just do not unlock.
                    unlock_pending_r <= execute_if.data.header.eop;
                    result_sent_r    <= 1'b0;
                end
            end
            PQC_ACQ: begin
                if (warp_pending_alm_empty[header_r.wid]) begin
                    lane_r          <= next_lane_idx;
                    pending_lanes_r <= pending_lanes_r & ~next_lane_onehot;
                    sent_r          <= '0;
                    recvd_r         <= '0;
                    state_q         <= PQC_LOAD;
                end
            end
            PQC_LOAD: begin
                if (req_fire) begin
                    sent_r <= sent_r + req_words;
                end
                if (rsp_fire) begin
                    for (int i = 0; i < MEM_LANES; ++i) begin
                        if (client_if.rsp_data.mask[i]) begin
                            state_r[WIDX_W'(rsp_word_base + WIDX_W'(i))] <= client_if.rsp_data.data[i];
                        end
                    end
                    if ((recvd_r + rsp_words) == WCNT_W'(WORDS)) begin
                        state_q <= PQC_PERM;
                        recvd_r <= '0;
                        sent_r  <= '0;
                    end else begin
                        recvd_r <= recvd_r + rsp_words;
                    end
                end
            end
            PQC_PERM: begin
                if (pe_rsp_valid) begin
                    state_r <= pe_rsp_state;
                    sent_r  <= '0;
                    state_q <= PQC_STORE;
                end
            end
            PQC_STORE: begin
                if (req_fire) begin
                    if ((sent_r + req_words) == WCNT_W'(WORDS)) begin
                        // Read every word back. The responses are the completion
                        // signal the store path cannot give.
                        state_q <= PQC_DRAIN;
                        sent_r  <= '0;
                        recvd_r <= '0;
                    end else begin
                        sent_r <= sent_r + req_words;
                    end
                end
            end
            PQC_DRAIN: begin
                if (req_fire) begin
                    sent_r <= sent_r + req_words;
                end
                if (rsp_fire) begin
                    // Discard the data; only the arrival matters.
                    if ((recvd_r + rsp_words) == WCNT_W'(WORDS)) begin
                        recvd_r <= '0;
                        sent_r  <= '0;
                        if (has_next_lane) begin
                            lane_r          <= next_lane_idx;
                            pending_lanes_r <= pending_lanes_r & ~next_lane_onehot;
                            state_q         <= PQC_LOAD;
                        end else begin
                            state_q <= PQC_DONE;
                        end
                    end else begin
                        recvd_r <= recvd_r + rsp_words;
                    end
                end
            end
            PQC_DONE: begin
                // Both must happen -- the result retires the instruction and
                // the unlock releases the warp decode stalled -- but they can
                // complete in EITHER ORDER and must be tracked apart. Holding
                // result_if.valid while waiting for a preempted unlock would
                // offer the same result again the next cycle: a duplicate
                // commit and a corrupted pending count. The RTU can take the
                // unlock port on the very cycle this result is accepted (it
                // pulses unlock a cycle before its own result), so the race is
                // reachable, not theoretical.
                if (result_fire) begin
                    result_sent_r <= 1'b1;
                end
                if ((result_sent_r || result_fire)
                 && (!unlock_pending_r || unlock_ack)) begin
                    state_q       <= PQC_IDLE;
                    result_sent_r <= 1'b0;
                end
            end
            default: state_q <= PQC_IDLE;
            endcase
        end
    end

`ifdef DBG_PQC
    pqc_state_e state_prev;
    always @(posedge clk) begin
        state_prev <= state_q;
        if (!reset && (state_q != state_prev)) begin
            $display("[PQCDBG] t=%0t state=%0d lane=%0d sent=%0d recvd=%0d idle=%b",
                     $time, state_q, lane_r, sent_r, recvd_r,
                     warp_pending_alm_empty[header_r.wid]);
        end
        if (!reset && req_fire) begin
            $display("[PQCDBG] t=%0t REQ rw=%b sent=%0d words=%0d addr0=%h",
                     $time, req_w.rw, sent_r, req_words, req_w.addr[0]);
        end
        if (!reset && rsp_fire) begin
            $display("[PQCDBG] t=%0t RSP base=%0d words=%0d recvd=%0d",
                     $time, rsp_word_base, rsp_words, recvd_r);
        end
    end
`endif

    assign execute_if.ready = (state_q == PQC_IDLE);

    sfu_result_t result_w;
    always_comb begin
        result_w        = '0;
        result_w.header = header_r;
        result_w.data   = '0;
    end
    assign unlock_req = (state_q == PQC_DONE) && unlock_pending_r;
    assign unlock_wid = header_r.wid;

    assign result_if.valid = (state_q == PQC_DONE) && !result_sent_r;
    assign result_if.data  = result_w;

    `UNUSED_VAR (execute_if.data.op_type)
    `UNUSED_VAR (execute_if.data.op_args)
    `UNUSED_VAR (execute_if.data.rs2_data)
    `UNUSED_VAR (execute_if.data.rs3_data)
    `UNUSED_VAR (pe_req_ready)

endmodule

`endif // VX_CFG_EXT_PQC_ENABLE
