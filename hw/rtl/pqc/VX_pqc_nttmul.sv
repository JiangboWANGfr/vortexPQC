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

    localparam NUM_MULTIPLIERS = (NUM_LANES + 1) / 2;
    localparam BEAT_WIDTH = TAG_WIDTH + 1 + 1 + 3 + 1 + 16 * NUM_MULTIPLIERS;

    wire valid_out;
    alu_header_t header_out;
    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_out;
    wire advance = result_if.ready || ~valid_out;

    wire is_butterfly_in = execute_if.data.op_type == INST_ALU_NTTBF_K;
    wire is_gs_in = execute_if.data.op_args.alu.imm20[3];
    wire [2:0] stage_in = execute_if.data.op_args.alu.imm20[2:0];
    wire two_beats = (NUM_LANES > 1) && (!is_butterfly_in || is_gs_in);
    wire [NUM_MULTIPLIERS-1:0][15:0] first_a, first_b, first_coefficients;
    wire [NUM_MULTIPLIERS-1:0][15:0] second_a, second_b, second_coefficients;

    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_operands
        wire [4:0][15:0] pair_a, pair_b, pair_zeta;
        for (genvar s = 0; s < 5; ++s) begin : g_stage
            localparam LO = (i & ((1 << s) - 1)) | ((i >> s) << (s + 1));
            localparam HI = LO | (1 << s);
            if (HI < NUM_LANES) begin : g_pair
                assign pair_a[s] = execute_if.data.rs1_data[LO][15:0];
                assign pair_b[s] = execute_if.data.rs1_data[HI][15:0];
                assign pair_zeta[s] = execute_if.data.rs2_data[LO][15:0];
            end else begin : g_unused
                assign pair_a[s] = '0;
                assign pair_b[s] = '0;
                assign pair_zeta[s] = '0;
            end
        end
        wire [15:0] a = pair_a[stage_in];
        wire [15:0] b = pair_b[stage_in];
        assign first_a[i] = is_butterfly_in
            ? (is_gs_in ? b - a : b) : execute_if.data.rs1_data[i][15:0];
        assign first_b[i] = is_butterfly_in
            ? pair_zeta[stage_in] : execute_if.data.rs2_data[i][15:0];
        assign first_coefficients[i] = a;
        if (i + NUM_MULTIPLIERS < NUM_LANES) begin : g_second
            assign second_a[i] = is_butterfly_in
                ? a + b : execute_if.data.rs1_data[i + NUM_MULTIPLIERS][15:0];
            assign second_b[i] = is_butterfly_in
                ? 16'd20159 : execute_if.data.rs2_data[i + NUM_MULTIPLIERS][15:0];
        end else begin : g_single
            assign second_a[i] = '0;
            assign second_b[i] = '0;
        end
        assign second_coefficients[i] = a + b;
    end

    reg pending_second;
    reg [NUM_MULTIPLIERS-1:0][15:0] saved_a, saved_b;
    reg [BEAT_WIDTH-1:0] saved_tag;
    always @(posedge clk) begin
        if (reset) begin
            pending_second <= 0;
        end else if (advance) begin
            if (pending_second) begin
                pending_second <= 0;
            end else if (execute_if.valid && two_beats) begin
                pending_second <= 1;
            end
        end
        if (advance && execute_if.valid && !pending_second && two_beats) begin
            saved_a <= second_a;
            saved_b <= second_b;
            saved_tag <= {execute_if.data.header, is_butterfly_in, is_gs_in,
                          stage_in, 1'b1, second_coefficients};
        end
    end

    wire [NUM_MULTIPLIERS-1:0][15:0] factor_a = pending_second ? saved_a : first_a;
    wire [NUM_MULTIPLIERS-1:0][15:0] factor_b = pending_second ? saved_b : first_b;
    wire [BEAT_WIDTH-1:0] tag_in = pending_second ? saved_tag
        : {execute_if.data.header, is_butterfly_in, is_gs_in,
           stage_in, !two_beats, first_coefficients};
    wire valid_mul;
    alu_header_t header_mul;
    wire is_butterfly_mul, is_gs_mul, last_mul;
    wire [2:0] stage_mul;
    wire [NUM_MULTIPLIERS-1:0][15:0] coefficients_mul;
    VX_shift_register #(
        .DATAW  (1 + BEAT_WIDTH),
        .DEPTH  (`LATENCY_IMUL),
        .RESETW (1)
    ) tag_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (advance),
        .data_in  ({pending_second || execute_if.valid, tag_in}),
        .data_out ({valid_mul, header_mul, is_butterfly_mul, is_gs_mul,
                    stage_mul, last_mul, coefficients_mul})
    );

`ifdef SIMULATION
    wire [LANE_BITS-1:0] distance_in = LANE_BITS'(1) << stage_in;
    reg pair_mask_valid;
    always @(*) begin
        pair_mask_valid = 1'b1;
        for (integer i = 0; i < NUM_LANES; ++i) begin
            if (execute_if.data.header.tmask[i]
             != execute_if.data.header.tmask[LANE_BITS'(i) ^ distance_in]) begin
                pair_mask_valid = 1'b0;
            end
        end
    end
    `RUNTIME_ASSERT(
        ~(execute_if.valid && execute_if.ready && is_butterfly_in)
        || ((NUM_LANES == 32) && (stage_in <= 3'd4) && pair_mask_valid),
        ("invalid NTTBF configuration: lanes=%0d stage=%0d tmask=%b",
         NUM_LANES, stage_in, execute_if.data.header.tmask))
`endif

    wire [NUM_MULTIPLIERS-1:0][31:0] product_out;
    wire [NUM_MULTIPLIERS-1:0][15:0] mont_factor_mul, barrett_quotient_mul;
    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_multiplier
        VX_multiplier #(
            .A_WIDTH (16),
            .B_WIDTH (16),
            .R_WIDTH (32),
            .SIGNED  (1),
            .LATENCY (`LATENCY_IMUL)
        ) multiplier (
            .clk    (clk),
            .enable (advance),
            .dataa  (factor_a[i]),
            .datab  (factor_b[i]),
            .result (product_out[i])
        );
        assign mont_factor_mul[i] = montgomery_factor($signed(product_out[i][15:0]));
        assign barrett_quotient_mul[i] = barrett_quotient($signed(product_out[i]));
    end

    wire valid_reduce;
    alu_header_t header_reduce;
    wire is_butterfly_reduce, is_gs_reduce, last_reduce;
    wire [2:0] stage_reduce;
    wire [NUM_MULTIPLIERS-1:0][15:0] coefficients_reduce;
    wire [NUM_MULTIPLIERS-1:0][31:0] product_reduce;
    wire [NUM_MULTIPLIERS-1:0][15:0] mont_factor_reduce, barrett_quotient_reduce;
    VX_shift_register #(
        .DATAW  (1 + BEAT_WIDTH + 64 * NUM_MULTIPLIERS),
        .DEPTH  (1),
        .RESETW (1)
    ) reduction_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (advance),
        .data_in  ({valid_mul, header_mul, is_butterfly_mul, is_gs_mul,
                    stage_mul, last_mul, coefficients_mul, product_out,
                    mont_factor_mul, barrett_quotient_mul}),
        .data_out ({valid_reduce, header_reduce, is_butterfly_reduce, is_gs_reduce,
                    stage_reduce, last_reduce, coefficients_reduce, product_reduce,
                    mont_factor_reduce, barrett_quotient_reduce})
    );

    wire [NUM_MULTIPLIERS-1:0][15:0] montgomery, result_low, result_high;
    reg [NUM_MULTIPLIERS-1:0][15:0] partial_results;
    // Two-beat operations remain adjacent through every globally stalled stage.
    always @(posedge clk) begin
        if (advance && valid_reduce && !last_reduce) begin
            partial_results <= montgomery;
        end
    end
    for (genvar i = 0; i < NUM_MULTIPLIERS; ++i) begin : g_finish
        assign montgomery[i] = montgomery_finish(
            $signed(product_reduce[i]), $signed(mont_factor_reduce[i]));
        wire [15:0] barrett = barrett_finish(
            $signed(coefficients_reduce[i]), $signed(barrett_quotient_reduce[i]));
        assign result_low[i] = is_gs_reduce
            ? barrett : coefficients_reduce[i] + montgomery[i];
        assign result_high[i] = is_gs_reduce
            ? partial_results[i] : coefficients_reduce[i] - montgomery[i];
    end

    wire [NUM_LANES-1:0][`VX_CFG_XLEN-1:0] data_reduce;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_result
        wire [4:0][15:0] butterfly;
        for (genvar s = 0; s < 5; ++s) begin : g_stage
            localparam BANK = (i & ((1 << s) - 1)) | ((i >> (s + 1)) << s);
            if (BANK < NUM_MULTIPLIERS) begin : g_pair
                assign butterfly[s] = (i & (1 << s)) != 0
                    ? result_high[BANK] : result_low[BANK];
            end else begin : g_unused
                assign butterfly[s] = '0;
            end
        end
        wire [15:0] nttmul;
        if (NUM_LANES == 1) begin : g_single
            assign nttmul = montgomery[i];
        end else if (i < NUM_MULTIPLIERS) begin : g_first
            assign nttmul = partial_results[i];
        end else begin : g_second
            assign nttmul = montgomery[i - NUM_MULTIPLIERS];
        end
        assign data_reduce[i] = `VX_CFG_XLEN'($signed(
            is_butterfly_reduce ? butterfly[stage_reduce] : nttmul));
    end

    VX_shift_register #(
        .DATAW  (1 + TAG_WIDTH + `VX_CFG_XLEN * NUM_LANES),
        .DEPTH  (1),
        .RESETW (1)
    ) result_pipe (
        .clk      (clk),
        .reset    (reset),
        .enable   (advance),
        .data_in  ({valid_reduce && last_reduce, header_reduce, data_reduce}),
        .data_out ({valid_out, header_out, data_out})
    );

    assign execute_if.ready = advance && !pending_second;
    assign result_if.valid = valid_out;
    assign result_if.data.header = header_out;
    assign result_if.data.data = data_out;

endmodule
