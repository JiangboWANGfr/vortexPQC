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

// NOTE ON LOOP SYNTAX: for-loops here use `i = i + 1` rather than the repo's
// usual `++i`. Yosys 0.33's Verilog frontend rejects TOK_INCREMENT, and this
// module has to pass through the ASIC gate as well as verilator.

// VX_pqc_keccak_f1600: one Keccak-f1600 permutation engine.
//
// 1600 bits in, 1600 bits out, 24 rounds, no options -- the whole reason
// pqc/docs/proposals/keccak_ise_proposal.md needs only one instruction. This
// module is that instruction's datapath and nothing else: no memory port, no
// sponge, no round-constant sequencing visible to software.
//
// State layout matches the library's `uint64_t state[25]` exactly, so
// A[x][y] lives at word index x + 5y and req_state[64*i +: 64] is word i. That
// is what lets the same 1600 bits be compared against
// mlk_keccakf1600_permute without a permutation in between.
//
// UNROLL rounds are computed per cycle, so latency is 24/UNROLL cycles and the
// round logic is replicated UNROLL times. It must divide 24. This is the
// area/latency knob the paper's sweep needs; it is not a correctness option.

module VX_pqc_keccak_f1600 #(
    parameter UNROLL = 1
) (
    input  wire            clk,
    input  wire            reset,

    input  wire            req_valid,
    input  wire [1599:0]   req_state,
    output wire            req_ready,

    output wire            rsp_valid,
    output wire [1599:0]   rsp_state,
    input  wire            rsp_ready
);
    localparam NUM_ROUNDS = 24;
    localparam NUM_STEPS  = NUM_ROUNDS / UNROLL;
    // `UP so UNROLL=24 (NUM_STEPS=1, CLOG2=0) does not declare a zero-width
    // counter. Caught by the UNROLL sweep, which is why the sweep exists.
    localparam CTR_W      = `UP(`CLOG2(NUM_STEPS));

    `STATIC_ASSERT((UNROLL * NUM_STEPS) == NUM_ROUNDS, ("UNROLL must divide 24"))

    // rho offsets, r[x][y], flattened as x*5 + y. A case rather than a packed
    // literal so the elaboration order cannot be silently reversed.
    function automatic int unsigned keccak_rho(input int unsigned idx);
        case (idx)
             0: keccak_rho =  0;  1: keccak_rho = 36;  2: keccak_rho =  3;
             3: keccak_rho = 41;  4: keccak_rho = 18;
             5: keccak_rho =  1;  6: keccak_rho = 44;  7: keccak_rho = 10;
             8: keccak_rho = 45;  9: keccak_rho =  2;
            10: keccak_rho = 62; 11: keccak_rho =  6; 12: keccak_rho = 43;
            13: keccak_rho = 15; 14: keccak_rho = 61;
            15: keccak_rho = 28; 16: keccak_rho = 55; 17: keccak_rho = 25;
            18: keccak_rho = 21; 19: keccak_rho = 56;
            20: keccak_rho = 27; 21: keccak_rho = 20; 22: keccak_rho = 39;
            23: keccak_rho =  8; 24: keccak_rho = 14;
            default: keccak_rho = 0;
        endcase
    endfunction

    function automatic logic [63:0] keccak_rc(input int unsigned r);
        case (r)
             0: keccak_rc = 64'h0000000000000001;  1: keccak_rc = 64'h0000000000008082;
             2: keccak_rc = 64'h800000000000808a;  3: keccak_rc = 64'h8000000080008000;
             4: keccak_rc = 64'h000000000000808b;  5: keccak_rc = 64'h0000000080000001;
             6: keccak_rc = 64'h8000000080008081;  7: keccak_rc = 64'h8000000000008009;
             8: keccak_rc = 64'h000000000000008a;  9: keccak_rc = 64'h0000000000000088;
            10: keccak_rc = 64'h0000000080008009; 11: keccak_rc = 64'h000000008000000a;
            12: keccak_rc = 64'h000000008000808b; 13: keccak_rc = 64'h800000000000008b;
            14: keccak_rc = 64'h8000000000008089; 15: keccak_rc = 64'h8000000000008003;
            16: keccak_rc = 64'h8000000000008002; 17: keccak_rc = 64'h8000000000000080;
            18: keccak_rc = 64'h000000000000800a; 19: keccak_rc = 64'h800000008000000a;
            20: keccak_rc = 64'h8000000080008081; 21: keccak_rc = 64'h8000000000008080;
            22: keccak_rc = 64'h0000000080000001; 23: keccak_rc = 64'h8000000080008008;
            default: keccak_rc = 64'h0;
        endcase
    endfunction

    // One round: theta, rho, pi, chi, iota. Indices are the library's:
    // A[x][y] is word x + 5y, so theta's column reduction strides by 5 and
    // chi's row neighbours stride by 1 modulo 5.
    function automatic logic [1599:0] keccak_round(input logic [1599:0] s,
                                                   input logic [63:0]   rc);
        logic [63:0] a [0:24];
        logic [63:0] c [0:4];
        logic [63:0] d [0:4];
        logic [63:0] t [0:24];
        logic [63:0] b [0:24];
        logic [63:0] o [0:24];
        int unsigned n;
        for (int i = 0; i < 25; i = i + 1) begin
            a[i] = s[64*i +: 64];
        end
        // theta
        for (int x = 0; x < 5; x = x + 1) begin
            c[x] = a[x] ^ a[x+5] ^ a[x+10] ^ a[x+15] ^ a[x+20];
        end
        for (int x = 0; x < 5; x = x + 1) begin
            d[x] = c[(x+4)%5] ^ {c[(x+1)%5][62:0], c[(x+1)%5][63]};
        end
        for (int x = 0; x < 5; x = x + 1) begin
            for (int y = 0; y < 5; y = y + 1) begin
                t[x + 5*y] = a[x + 5*y] ^ d[x];
            end
        end
        // rho and pi: B[y][(2x+3y) mod 5] = ROL64(A[x][y], r[x][y])
        for (int x = 0; x < 5; x = x + 1) begin
            for (int y = 0; y < 5; y = y + 1) begin
                n = keccak_rho(x*5 + y);
                b[y + 5*((2*x + 3*y) % 5)] = (n == 0) ? t[x + 5*y]
                    : ((t[x + 5*y] << n) | (t[x + 5*y] >> (64 - n)));
            end
        end
        // chi
        for (int x = 0; x < 5; x = x + 1) begin
            for (int y = 0; y < 5; y = y + 1) begin
                o[x + 5*y] = b[x + 5*y] ^ ((~b[((x+1)%5) + 5*y]) & b[((x+2)%5) + 5*y]);
            end
        end
        // iota, on A[0][0] only
        o[0] = o[0] ^ rc;
        for (int i = 0; i < 25; i = i + 1) begin
            keccak_round[64*i +: 64] = o[i];
        end
    endfunction

    reg  [1599:0]   state;
    reg  [CTR_W-1:0] step;
    reg             busy, done;

    // UNROLL rounds chained combinationally. The round index must be the
    // absolute one, because iota's constant differs every round.
    // One flat vector rather than an array of 1600-bit elements, for two
    // independent tool reasons. Verilator's UNOPTFLAT analysis is whole-signal
    // for an UNPACKED array, so chain[u+1] = f(chain[u]) reads as a combinational
    // loop even though it is a chain; and yosys 0.33 does not parse a packed 2D
    // wire declaration at all. A flat vector with a part-select satisfies both.
    wire [1599:0] next_state;
    wire [(UNROLL+1)*1600-1:0] chain;
    assign chain[0 +: 1600] = state;
    for (genvar u = 0; u < UNROLL; u = u + 1) begin : g_rounds
        assign chain[1600*(u+1) +: 1600] = keccak_round(chain[1600*u +: 1600],
                                                        keccak_rc(32'(step) * UNROLL + u));
    end
    assign next_state = chain[1600*UNROLL +: 1600];

    wire is_last = (step == CTR_W'(NUM_STEPS - 1));

    always @(posedge clk) begin
        if (reset) begin
            busy <= 1'b0;
            done <= 1'b0;
            step <= '0;
        end else begin
            if (!busy && !done && req_valid) begin
                state <= req_state;
                step  <= '0;
                busy  <= 1'b1;
            end else if (busy) begin
                state <= next_state;
                if (is_last) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end else begin
                    step <= step + CTR_W'(1);
                end
            end else if (done && rsp_ready) begin
                done <= 1'b0;
            end
        end
    end

    assign req_ready = !busy && !done;
    assign rsp_valid = done;
    assign rsp_state = state;

endmodule
