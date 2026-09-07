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

  // KAT_ZERO_IN
  localparam logic [1599:0] KAT_ZERO_IN = 1600'h0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000;
  // KAT_ZERO_OUT1
  localparam logic [1599:0] KAT_ZERO_OUT1 = 1600'heaf1ff7b5ceca24975f644e97f30a13b16f53526e70465c21841f924a2c509e4940c7922ae3a26148c3ee88a1ccf32c8b87c5a554fd00ecb613670957bc4661164befef28cc970f205e5635a21d9ae6101f22f1a11a5569f43b831cd0347c82681a57c16dbcf555fa9a6e6260d712103eb5aa93f2317d63530935ab7d08ffc64ad30a6f71b19059c8c5bda0cd6192e7690fee5a0a44647c4ff97a42d7f8e6fd48b284e056253d057bd1547306f80494dd598261ea65aa9ee84d5ccf933c0478af1258f7940e1dde7;
  // KAT_ZERO_OUT2
  localparam logic [1599:0] KAT_ZERO_OUT2 = 1600'h20d06cd26a8fbf5c609f4e62a44c10595b3402464e1c3db6202a9ec5faa3cce8900e3129e7badd7b91a0226e649e42e9e3b8c8ee55b7b03c48ead5fc5d0be77497ddad33d8994b40fd5449a6bf1747437cf8a9f009831265e00654042719dbd933c43d836eafb1f5deea66c4ba8f974f68ce61b6b9ce68a1e4fecc0fee98b4251f1b9ee6f79a8759faf4f247c3d810f785773dae1275af0df957b9a2da65fb384f9c4f99e5e7f1568a20d9b25569d094093d8d1270d76b6c6a332cd07057b56d2d5c954df96ecb3c;
  // KAT_SEED_IN
  localparam logic [1599:0] KAT_SEED_IN = 1600'hf1a5308060c2be4f524a715f57cc744bb2efb23e4ed62a471394f31d45dfe043743a33fc3ce9963fd4df74db33f34c3b3584b5ba2afd02379629f6992206b833f6cf377819106e2f57747857101a242bb819b9360723da2718befa14fe2d902379643af3f537461fda097bd2ec40fc1b3aaebcb1e34ab2179b53fd90da546813fbf93e6fd15e1e0f5c9e7f4ec867d40bbd43c02dbf718a071de9010cb67b40037e8e41ebad84f5ffdf3382caa48eabfb3fd8c3a99b9861f7a07e048892a217f30123456789abcdef;
  // KAT_SEED_OUT1
  localparam logic [1599:0] KAT_SEED_OUT1 = 1600'hcadc9f3a79d0adae0f79d217d35ecbeec3778e00be4dbefe80a2bcad7bff3842083043f1453d68646d6087b5de268e04a64db7a4329c9ed094eb8b22ff866e78a0bde2f584e69d996cec94b05ae23069f7285efff475c05be7c492154752213609424af8318a41fed1206cecc63426abd059a2fbb7b9dffadc4421d25bc932e6fa0b77c73cfc9217259c71c747df6a1360a68aa5b4b877cc8f86a75d7dc7d7d3a7d4cc9402f754949d4fb35196907c2f69198950c1912b86c82d190f708bd3fabd5b47526faf554a;

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
        else             $display("FAILED! %0d error(s)", errors);
        $finish;
    end

endmodule
