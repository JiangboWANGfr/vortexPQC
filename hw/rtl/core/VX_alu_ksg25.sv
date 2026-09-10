// Copyright © 2026
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

module VX_alu_ksg25 import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = ""
) (
    input wire          clk,
    input wire          reset,
    VX_execute_if.slave execute_if,
    VX_result_if.master result_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_VAR ({execute_if.data.op_args, execute_if.data.rs3_data})
    `UNUSED_VAR (execute_if.data.rs1_data[31:25])

    `STATIC_ASSERT(`VX_CFG_XLEN == 32, ("KSG25 requires RV32"))
    `STATIC_ASSERT(`VX_CFG_NUM_THREADS == 32, ("KSG25 requires 32 threads"))
    `STATIC_ASSERT(`VX_CFG_SIMD_WIDTH == 32, ("KSG25 requires SIMD width 32"))
    `STATIC_ASSERT(`VX_CFG_NUM_ALU_LANES == 32, ("KSG25 requires 32 ALU lanes"))
    `RUNTIME_ASSERT(!execute_if.valid || &execute_if.data.header.tmask,
                    ("%t: %s: KSG25 requires a full warp mask", $time, INSTANCE_ID))

    typedef struct packed {
        alu_header_t        header;
        logic               high_half;
        logic               is_theta;
        logic               is_chii;
        logic [31:0]        round_constant;
        logic [24:0][31:0]  state_half;
        logic [4:0][63:0]   parity;
    } stage_t;

    stage_t stage_in, stage_out;
    wire stage_valid, stage_ready;
    alu_result_t result;

    assign stage_in.header = execute_if.data.header;
    wire is_theta = execute_if.data.op_type <= INST_OP_BITS'(INST_KTHETA_H);
    wire is_chii = execute_if.data.op_type >= INST_OP_BITS'(INST_KCHII_L);
    assign stage_in.is_theta = is_theta;
    assign stage_in.is_chii = is_chii;
    assign stage_in.high_half = ~execute_if.data.op_type[0];
    localparam logic [63:0] RC [24] = '{
        64'h0000000000000001,
        64'h0000000000008082,
        64'h800000000000808a,
        64'h8000000080008000,
        64'h000000000000808b,
        64'h0000000080000001,
        64'h8000000080008081,
        64'h8000000000008009,
        64'h000000000000008a,
        64'h0000000000000088,
        64'h0000000080008009,
        64'h000000008000000a,
        64'h000000008000808b,
        64'h800000000000008b,
        64'h8000000000008089,
        64'h8000000000008003,
        64'h8000000000008002,
        64'h8000000000000080,
        64'h000000000000800a,
        64'h800000008000000a,
        64'h8000000080008081,
        64'h8000000000008080,
        64'h0000000080000001,
        64'h8000000080008008
    };
    wire [63:0] rc = RC[execute_if.data.rs2_data[0][4:0]];
    assign stage_in.round_constant = stage_in.high_half ? rc[63:32] : rc[31:0];
    for (genvar t = 0; t < 32; ++t) begin : g_round_contract
        `RUNTIME_ASSERT(!execute_if.valid || !is_chii
                     || (execute_if.data.rs2_data[t] == execute_if.data.rs2_data[0]
                      && execute_if.data.rs2_data[t] < 24),
                        ("KCHII requires a uniform round in 0..23"))
    end
    localparam integer RHO [25] = '{
        0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43,
        25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14
    };

    for (genvar t = 0; t < 25; ++t) begin : g_state
        localparam X = t % 5;
        localparam ROW = (t / 5) * 5;
        localparam SRC = ((X + 3 * (t / 5)) % 5) + 5 * X;
        wire [63:0] word_in = {execute_if.data.rs2_data[SRC], execute_if.data.rs1_data[SRC]};
        wire [63:0] rotated = (word_in << RHO[SRC]) | (word_in >> ((64 - RHO[SRC]) % 64));
        assign stage_in.state_half[t] = is_theta
            ? (stage_in.high_half ? execute_if.data.rs2_data[t] : execute_if.data.rs1_data[t])
            : is_chii
            ? (execute_if.data.rs1_data[t] ^ (~execute_if.data.rs1_data[ROW + (X+1)%5]
                                          & execute_if.data.rs1_data[ROW + (X+2)%5]))
            : (stage_in.high_half ? rotated[63:32] : rotated[31:0]);
    end

    for (genvar x = 0; x < 5; ++x) begin : g_parity
        assign stage_in.parity[x] = {execute_if.data.rs2_data[x], execute_if.data.rs1_data[x]}
                                 ^ {execute_if.data.rs2_data[x+5], execute_if.data.rs1_data[x+5]}
                                 ^ {execute_if.data.rs2_data[x+10], execute_if.data.rs1_data[x+10]}
                                 ^ {execute_if.data.rs2_data[x+15], execute_if.data.rs1_data[x+15]}
                                 ^ {execute_if.data.rs2_data[x+20], execute_if.data.rs1_data[x+20]};
    end

    VX_pipe_buffer #(
        .DATAW ($bits(stage_t))
    ) parity_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (execute_if.valid),
        .ready_in  (execute_if.ready),
        .data_in   (stage_in),
        .data_out  (stage_out),
        .ready_out (stage_ready),
        .valid_out (stage_valid)
    );

    wire [4:0][31:0] correction;
    for (genvar x = 0; x < 5; ++x) begin : g_correction
        localparam PREV = (x + 4) % 5;
        localparam NEXT = (x + 1) % 5;
        assign correction[x] = stage_out.high_half
                             ? stage_out.parity[PREV][63:32]
                               ^ {stage_out.parity[NEXT][62:32], stage_out.parity[NEXT][31]}
                             : stage_out.parity[PREV][31:0]
                               ^ {stage_out.parity[NEXT][30:0], stage_out.parity[NEXT][63]};
    end

    assign result.header = stage_out.header;
    for (genvar t = 0; t < `VX_CFG_NUM_ALU_LANES; ++t) begin : g_result
        if (t < 25) begin : g_state
            assign result.data[t] = stage_out.state_half[t]
                                 ^ (stage_out.is_theta ? correction[t % 5] : 32'b0)
                                 ^ ((stage_out.is_chii && t == 0) ? stage_out.round_constant : 32'b0);
        end else begin : g_padding
            assign result.data[t] = '0;
        end
    end

    VX_pipe_buffer #(
        .DATAW ($bits(alu_result_t))
    ) result_buffer (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (stage_valid),
        .ready_in  (stage_ready),
        .data_in   (result),
        .data_out  (result_if.data),
        .ready_out (result_if.ready),
        .valid_out (result_if.valid)
    );

endmodule
