// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

`include "VX_define.vh"

module VX_pqc_nttmul import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter NUM_LANES = 1
) (
    input wire          clk,
    input wire          reset,

    VX_execute_if.slave execute_if,
    VX_result_if.master result_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_VAR (execute_if.data.rs3_data)

    localparam TAG_WIDTH = $bits(alu_header_t);
    localparam LANE_BITS = `UP(`CLOG2(NUM_LANES));

    function automatic logic signed [15:0] montgomery_factor(
        input logic signed [15:0] product
    );
        logic [15:0] product_low;
        begin
            product_low = product[15:0];
            montgomery_factor = product_low - (product_low << 8)
                                              - (product_low << 10)
                                              - (product_low << 11);
        end
    endfunction

    function automatic logic signed [15:0] montgomery_finish(
        input logic signed [31:0] product,
        input logic signed [15:0] mont_factor
    );
        logic signed [31:0] mont_factor_ext;
        logic signed [31:0] factor_q;
        begin
            mont_factor_ext = {{16{mont_factor[15]}}, mont_factor};
            factor_q = mont_factor_ext
                     + (mont_factor_ext <<< 8)
                     + (mont_factor_ext <<< 10)
                     + (mont_factor_ext <<< 11);
            montgomery_finish = 16'($signed(product - factor_q) >>> 16);
        end
    endfunction

    function automatic logic signed [15:0] barrett_quotient(
        input logic signed [31:0] constant_product
    );
        logic signed [31:0] rounded;
        logic signed [15:0] quotient;
        begin
            rounded = constant_product + 32'sd33554432;
            quotient = 16'(rounded >>> 26);
            barrett_quotient = quotient;
        end
    endfunction

    function automatic logic signed [15:0] barrett_finish(
        input logic signed [15:0] value,
        input logic signed [15:0] quotient
    );
        logic signed [31:0] quotient_ext;
        logic signed [31:0] quotient_q;
        begin
            quotient_ext = {{16{quotient[15]}}, quotient};
            quotient_q = quotient_ext
                       + (quotient_ext <<< 8)
                       + (quotient_ext <<< 10)
                       + (quotient_ext <<< 11);
            barrett_finish = 16'({{16{value[15]}}, value}
                               - quotient_q);
        end
    endfunction

    wire valid_mul;
    alu_header_t header_mul;
    wire [NUM_LANES-1:0][15:0] coefficients_mul;

    wire valid_out;
    alu_header_t header_out;
    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_out;
    wire ready_in = result_if.ready || ~valid_out;

    wire is_butterfly_in = execute_if.data.op_type == INST_ALU_NTTBF_K;
    wire is_gs_in = execute_if.data.op_args.alu.imm20[3];
    wire [2:0] stage_in = execute_if.data.op_args.alu.imm20[2:0];
    wire [LANE_BITS-1:0] distance_in = LANE_BITS'(1) << stage_in;
    wire [NUM_LANES-1:0][15:0] coefficients_in;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_coefficients_in
        assign coefficients_in[i] = execute_if.data.rs1_data[i][15:0];
    end

    wire is_butterfly_mul;
    wire is_gs_mul;
    wire [2:0] stage_mul;

    VX_shift_register #(
        .DATAW  (1 + TAG_WIDTH + 1 + 1 + 3 + 16 * NUM_LANES),
        .DEPTH  (`LATENCY_IMUL),
        .RESETW (1)
    ) tag_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (ready_in),
        .data_in  ({execute_if.valid, execute_if.data.header,
                    is_butterfly_in,  is_gs_in,  stage_in,  coefficients_in}),
        .data_out ({valid_mul,        header_mul,
                    is_butterfly_mul, is_gs_mul, stage_mul, coefficients_mul})
    );

`ifdef SIMULATION
    reg pair_mask_valid;
    always @(*) begin
        pair_mask_valid = 1'b1;
        for (integer i = 0; i < NUM_LANES; ++i) begin
            if (execute_if.data.header.tmask[i]
             != execute_if.data.header.tmask[LANE_BITS'(i) ^ distance_in])
                pair_mask_valid = 1'b0;
        end
    end

    `RUNTIME_ASSERT(
        ~(execute_if.valid && ready_in && is_butterfly_in)
        || ((NUM_LANES == 32) && (stage_in <= 3'd4) && pair_mask_valid),
        ("invalid NTTBF configuration: lanes=%0d stage=%0d tmask=%b",
         NUM_LANES, stage_in, execute_if.data.header.tmask))
`endif

    wire [NUM_LANES-1:0][31:0] product_out;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_multiplier
        wire [LANE_BITS-1:0] lane_idx = LANE_BITS'(i);
        wire [LANE_BITS-1:0] pair_lo_idx = lane_idx & ~distance_in;
        wire [LANE_BITS-1:0] pair_hi_idx = pair_lo_idx | distance_in;
        wire lane_is_low = (lane_idx & distance_in) == '0;

        wire signed [15:0] pair_a = $signed(execute_if.data.rs1_data[pair_lo_idx][15:0]);
        wire signed [15:0] pair_b = $signed(execute_if.data.rs1_data[pair_hi_idx][15:0]);
        wire signed [15:0] pair_zeta = $signed(execute_if.data.rs2_data[pair_lo_idx][15:0]);
        wire signed [15:0] sum16 = pair_a + pair_b;
        wire signed [15:0] difference16 = pair_b - pair_a;

        wire signed [15:0] nttmul_a = $signed(execute_if.data.rs1_data[i][15:0]);
        wire signed [15:0] nttmul_b = $signed(execute_if.data.rs2_data[i][15:0]);
        wire signed [15:0] factor_a = is_butterfly_in
            ? (lane_is_low ? (is_gs_in ? difference16 : pair_b)
                           : (is_gs_in ? sum16 : 16'sd0))
            : nttmul_a;
        wire signed [15:0] factor_b = is_butterfly_in
            ? (lane_is_low ? pair_zeta
                           : (is_gs_in ? 16'sd20159 : 16'sd0))
            : nttmul_b;

        VX_multiplier #(
            .A_WIDTH (16),
            .B_WIDTH (16),
            .R_WIDTH (32),
            .SIGNED  (1),
            .LATENCY (`LATENCY_IMUL)
        ) multiplier (
            .clk    (clk),
            .enable (ready_in),
            .dataa  (factor_a),
            .datab  (factor_b),
            .result (product_out[i])
        );
    end

    wire [NUM_LANES-1:0][15:0] mont_factor_mul;
    wire [NUM_LANES-1:0][15:0] barrett_quotient_mul;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_reduction_seed
        assign mont_factor_mul[i] = montgomery_factor($signed(product_out[i][15:0]));
        assign barrett_quotient_mul[i] = barrett_quotient($signed(product_out[i]));
    end

    wire valid_reduce;
    alu_header_t header_reduce;
    wire is_butterfly_reduce;
    wire is_gs_reduce;
    wire [2:0] stage_reduce;
    wire [NUM_LANES-1:0][15:0] coefficients_reduce;
    wire [NUM_LANES-1:0][31:0] product_reduce;
    wire [NUM_LANES-1:0][15:0] mont_factor_reduce;
    wire [NUM_LANES-1:0][15:0] barrett_quotient_reduce;

    VX_shift_register #(
        .DATAW  (1 + TAG_WIDTH + 1 + 1 + 3 + 80 * NUM_LANES),
        .DEPTH  (1),
        .RESETW (1)
    ) reduction_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (ready_in),
        .data_in  ({valid_mul, header_mul, is_butterfly_mul, is_gs_mul,
                    stage_mul, coefficients_mul, product_out,
                    mont_factor_mul, barrett_quotient_mul}),
        .data_out ({valid_reduce, header_reduce, is_butterfly_reduce,
                    is_gs_reduce, stage_reduce, coefficients_reduce,
                    product_reduce, mont_factor_reduce,
                    barrett_quotient_reduce})
    );

    wire [LANE_BITS-1:0] distance_reduce = LANE_BITS'(1) << stage_reduce;
    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_reduce;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_result
        wire [LANE_BITS-1:0] lane_idx = LANE_BITS'(i);
        wire [LANE_BITS-1:0] pair_lo_idx = lane_idx & ~distance_reduce;
        wire [LANE_BITS-1:0] pair_hi_idx = pair_lo_idx | distance_reduce;
        wire lane_is_low = (lane_idx & distance_reduce) == '0;

        wire signed [15:0] pair_a = $signed(coefficients_reduce[pair_lo_idx]);
        wire signed [15:0] pair_b = $signed(coefficients_reduce[pair_hi_idx]);
        wire signed [15:0] sum16 = pair_a + pair_b;
        wire signed [15:0] montgomery = montgomery_finish(
            $signed(product_reduce[pair_lo_idx]),
            $signed(mont_factor_reduce[pair_lo_idx]));
        wire signed [15:0] barrett = barrett_finish(
            sum16, $signed(barrett_quotient_reduce[pair_hi_idx]));
        wire signed [15:0] ct_result = lane_is_low
            ? pair_a + montgomery
            : pair_a - montgomery;
        wire signed [15:0] butterfly = is_gs_reduce
            ? (lane_is_low ? barrett : montgomery)
            : ct_result;
        wire signed [15:0] nttmul = montgomery_finish(
            $signed(product_reduce[i]), $signed(mont_factor_reduce[i]));

        assign data_reduce[i] = `VX_CFG_XLEN'($signed(
            is_butterfly_reduce ? butterfly : nttmul));
    end

    VX_shift_register #(
        .DATAW  (1 + TAG_WIDTH + `VX_CFG_XLEN * NUM_LANES),
        .DEPTH  (1),
        .RESETW (1)
    ) result_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (ready_in),
        .data_in  ({valid_reduce, header_reduce, data_reduce}),
        .data_out ({valid_out, header_out, data_out})
    );

    assign execute_if.ready = ready_in;
    assign result_if.valid = valid_out;
    assign result_if.data.header = header_out;
    assign result_if.data.data = data_out;

endmodule
