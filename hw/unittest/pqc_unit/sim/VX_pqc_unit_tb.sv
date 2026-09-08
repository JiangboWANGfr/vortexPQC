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

// Functional check for VX_pqc_keccak_f1600 against the library it has to match.
//
// WHY THIS LIVES IN A SUBDIRECTORY. The synthesis catalogs point their include
// at hw/unittest/pqc_unit, and hw/scripts/gen_sources.sh:157 sweeps an include
// dir with `find -maxdepth 1 -type f` -- so any .sv sitting beside the top gets
// copied into the synthesis source set. The repo's other synthesis DUTs
// (hw/unittest/{cache,core}) hold only main.cpp, Makefile and VX_*_top.sv for
// exactly this reason. One level down keeps a testbench out of the gate.
//
// The vectors below were emitted by mlk_keccakf1600_permute from the pristine
// pqc/third_party/mlkem-native submodule -- the same function tests/pqc use as
// their reference -- so a word-order or rho-table mistake in the RTL cannot pass.
// KAT_ZERO_OUT1's word 0 is f1258f7940e1dde7, the FIPS 202 all-zero known answer,
// which anchors the whole set to the standard rather than to this repo.
//
// Word i of the library's uint64_t state[25] is bits [64*i +: 64], so A[x][y] is
// word x + 5y. The literals below are printed word 24 first.

`include "VX_pqc_unit_define.vh"

module VX_pqc_unit_tb;

    `include "kat.vh"

    localparam UNROLL   = `PQC_TOP_UNROLL;
    localparam LATENCY  = 24 / UNROLL;

    logic clk, reset;
    logic req_valid, rsp_ready;
    logic [1599:0] req_state;
    logic req_ready, rsp_valid;
    logic [1599:0] rsp_state;

    initial begin
        clk       = 1'b0;
        reset     = 1'b1;
        req_valid = 1'b0;
        rsp_ready = 1'b0;
        req_state = '0;
    end

    always #5 clk = ~clk;

    VX_pqc_unit_top dut (
        .clk       (clk),
        .reset     (reset),
        .req_valid (req_valid),
        .req_state (req_state),
        .req_ready (req_ready),
        .rsp_valid (rsp_valid),
        .rsp_state (rsp_state),
        .rsp_ready (rsp_ready)
    );

    int errors = 0;

    task automatic run_one(input string name,
                           input logic [1599:0] din,
                           input logic [1599:0] want);
        int unsigned cycles;
        begin
            @(negedge clk);
            wait (req_ready);
            req_state = din;
            req_valid = 1;
            @(negedge clk);
            req_valid = 0;
            cycles = 0;
            while (!rsp_valid) begin
                @(negedge clk);
                cycles++;
            end
            if (rsp_state !== want) begin
                $display("*** %s MISMATCH", name);
                for (int i = 0; i < 25; ++i) begin
                    if (rsp_state[64*i +: 64] !== want[64*i +: 64]) begin
                        $display("      word %0d (x=%0d,y=%0d): got %016h want %016h",
                                 i, i % 5, i / 5,
                                 rsp_state[64*i +: 64], want[64*i +: 64]);
                    end
                end
                errors++;
            end else begin
                $display("    %-14s OK   latency %0d cycles", name, cycles);
            end
            // Latency must be exactly 24/UNROLL: a permutation that takes longer
            // is a stalled pipeline, one that takes fewer skipped a round.
            if (cycles != LATENCY) begin
                $display("*** %s latency %0d, expected %0d", name, cycles, LATENCY);
                errors++;
            end
            rsp_ready = 1;
            @(negedge clk);
            rsp_ready = 0;
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 0;
        @(negedge clk);

        $display("VX_pqc_keccak_f1600  UNROLL=%0d  expected latency %0d", UNROLL, LATENCY);
        run_one("zero->1",  KAT_ZERO_IN,   KAT_ZERO_OUT1);
        run_one("zero->2",  KAT_ZERO_OUT1, KAT_ZERO_OUT2);
        run_one("seed->1",  KAT_SEED_IN,   KAT_SEED_OUT1);
        // back to back, to prove the handshake releases and re-arms
        run_one("zero->1 b", KAT_ZERO_IN,  KAT_ZERO_OUT1);

        if (errors == 0) $display("PASSED!");
        else             $fatal(1, "FAILED! %0d error(s)", errors);
        $finish;
    end

endmodule
